#include "juce_gui_basics/juce_gui_basics.h"
#include <memory>
#include "VSTScanner.h"
#include "SynthGlobals.h"

#include "VersionInfo.h"

#include "SynthGlobals.h"
#include "SignalGenerator.h"
#include "Amplifier.h"
#include "OutputChannel.h"
#include "PatchCable.h"
#include "PatchCableSource.h"
#include "UIControlMacros.h"

using namespace juce;

Component* createMainContentComponent();
std::unique_ptr<juce::ApplicationProperties> appProperties;
void SetStartupSaveStateFile(const String& bskPath, Component* mainComponent);

juce::ApplicationProperties& getAppProperties()
{
   return *appProperties;
}

//==============================================================================
class BespokeApplication : public JUCEApplication
{
public:
   //==============================================================================
   BespokeApplication() = default;

   const String getApplicationName() override { return Bespoke::APP_NAME; }
   const String getApplicationVersion() override { return Bespoke::VERSION; }
   bool moreThanOneInstanceAllowed() override { return true; }

   //==============================================================================
   void initialise(const String& commandLine) override
   {
      // Parse command line arguments that should cause us to exit
      auto cliArgv = JUCEApplication::getCommandLineParameterArray();
      for (int i = 0; i < cliArgv.size(); ++i)
      {
         bool should_exit = false;
         juce::String argument = cliArgv[i];
         if (argument == "-h" || argument == "--help")
         {
            std::cout << "A modular DAW for Mac, Windows, and Linux.\n"
                      << "\n"
                      << "Usage: BespokeSynth [OPTIONS] [path].json [path].bsk(t)\n"
                      << "\n"
                      << "Arguments:\n"
                      << "  [path].bsk(t)   the project file to open (must end in .bsk or .bskt)\n"
                      << "  [path].json     path to userprefs.json (must end in .json)\n"
                      << "\n"
                      << "Options:\n"
                      << "  -o, --option <option> <value>   Temporarily override settings in preferences file\n"
                      << "  -h, --help                      Print help\n"
                      << "  -v, --version                   Print version\n"
                      << std::flush;
            should_exit = true;
         }
         else if (argument == "-v" || argument == "--version")
         {
            std::cout << "bespoke synth " << GetBuildInfoString() << std::endl;
            should_exit = true;
         }
         else if (argument == "-o" || argument == "--option")
         {
            if ((cliArgv[i + 1].isEmpty()) || (cliArgv[i + 2].isEmpty()))
            {
               CliErrorExpectedOpt(argument);
               should_exit = true;
            }
         }

         if (should_exit == true)
         {
            JUCEApplicationBase::quit();
            return;
         }
      }

      auto scannerSubprocess = std::make_unique<PluginScannerSubprocess>();

      if (scannerSubprocess->initialiseFromCommandLine(commandLine, kScanProcessUID))
      {
         storedScannerSubprocess = std::move(scannerSubprocess);
         return;
      }

      mainWindow = std::make_unique<MainWindow>("bespoke synth");

      juce::PropertiesFile::Options options;
      options.applicationName = "Bespoke Synth";
      options.filenameSuffix = "settings";
      options.osxLibrarySubFolder = "Preferences";

      appProperties = std::make_unique<juce::ApplicationProperties>();
      appProperties->setStorageParameters(options);

      // ----- Experiment : no gui + play a sound with modules and patchcables then quit ----- //

      SetGlobalSampleRateAndBufferSize(44100, 512);
      signalGenerator = std::make_unique<SignalGenerator>();
      amplifier = std::make_unique<Amplifier>();
      outputChannel = std::make_unique<OutputChannel>();

      sigGenCableSource = std::make_unique<PatchCableSource>(signalGenerator.get(), kConnectionType_Audio);
      ampCableSource = std::make_unique<PatchCableSource>(amplifier.get(), kConnectionType_Audio);

      cable1 = std::make_unique<PatchCable>(sigGenCableSource.get());
      cable2 = std::make_unique<PatchCable>(ampCableSource.get());

      sigGenCableSource->SetPatchCableTarget(cable1.get(), amplifier.get(), false);
      sigGenCableSource->SetPatchCableTarget(cable2.get(), outputChannel.get(), false);

      float* mFreq = new float(230.0f);

      // signalGenerator->FloatSliderUpdated(
      //    new FloatSlider(signalGenerator.get(), "freq", 0, 0, 40, 15, mFreq, 1, 4000),
      //    0.0,
      //    0);
      signalGenerator->SetVol(0.5f);
      // signalGenerator->PlayNote(NoteMessage(5000, 69, 120));

      std::cout << "Experiment running : sin wave playing for 5 sec then exit..." << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(5));

      // quit application
      shutdown();
      systemRequestedQuit();
   }

   // Prints an error for arguments that expected an argument but were not given one
   void CliErrorExpectedOpt(String argument)
   {
      std::cout << "Error: value is required for '" << argument << "' but none was supplied"
                << "\n\nFor more information, try '--help'"
                << std::endl;
   }

   void shutdown() override
   {
      // Add your application's shutdown code here..
      mainWindow.reset();
      appProperties.reset();
   }

   //==============================================================================
   void systemRequestedQuit() override
   {
      // This is called when the app is being asked to quit: you can ignore this
      // request and let the app carry on running, or call quit() to allow the app to close.
      quit();
   }

   void anotherInstanceStarted(const String& commandLine) override
   {
      // When another instance of the app is launched while this one is running,
      // this method is invoked, and the commandLine parameter tells you what
      // the other instance's command-line arguments were.

      // This is also called when opening the app with a file.
      if (commandLine.isNotEmpty() && commandLine.endsWith(".bsk"))
         SetStartupSaveStateFile(commandLine, mainWindow->getContentComponent());
   }

   //==============================================================================
   /*
    This class implements the desktop window that contains an instance of
    our MainContentComponent class.
    */
   class MainWindow : public DocumentWindow
   {
   public:
      MainWindow(String name)
      : DocumentWindow(name,
                       Colours::lightgrey,
                       DocumentWindow::allButtons,
                       false)
      {
         setUsingNativeTitleBar(true);
         setContentOwned(createMainContentComponent(), true);
         setResizable(true, true);

         centreWithSize(getWidth(), getHeight());
         setVisible(true);
      }

      void closeButtonPressed() override
      {
         // This is called when the user tries to close this window. Here, we'll just
         // ask the app to quit when this happens, but you can change this to do
         // whatever you need.
         JUCEApplication::getInstance()->systemRequestedQuit();
      }

      /* Note: Be careful if you override any DocumentWindow methods - the base
       class uses a lot of them, so by overriding you might break its functionality.
       It's best to do all your work in your content component instead, but if
       you really have to override any DocumentWindow methods, make sure your
       subclass also calls the superclass's method.
       */

   private:
      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
   };

private:
   std::unique_ptr<MainWindow> mainWindow;
   std::unique_ptr<PluginScannerSubprocess> storedScannerSubprocess;

   std::unique_ptr<SignalGenerator> signalGenerator;
   std::unique_ptr<Amplifier> amplifier;
   std::unique_ptr<OutputChannel> outputChannel;
   std::unique_ptr<PatchCableSource> sigGenCableSource;
   std::unique_ptr<PatchCableSource> ampCableSource;
   std::unique_ptr<PatchCable> cable1;
   std::unique_ptr<PatchCable> cable2;

   FloatSlider* mFreqSlider{ nullptr };
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION(BespokeApplication)
