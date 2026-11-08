#include <iostream>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "PluginProcessor.h"

//==============================================================================
// Renders the editor to a PNG without a display:
//   ReeseTests --snapshot out.png [width] [preset]
static int writeSnapshot (const juce::File& file, int width, int preset)
{
    ReeseAudioProcessor processor;
    if (preset > 0)
        processor.setCurrentProgram (preset);

    // play a held note for a moment so the scope has something to show
    processor.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    for (int block = 0; block < 40; ++block)
    {
        juce::MidiBuffer midi;
        if (block == 0)
            midi.addEvent (juce::MidiMessage::noteOn (1, 40, 0.9f), 0);
        processor.processBlock (buffer, midi);
    }

    juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // deliver change messages

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (width > 0)
        editor->setSize (width, juce::roundToInt (width * (double) editor->getHeight() / editor->getWidth()));
    juce::MessageManager::getInstance()->runDispatchLoopUntil (1500);

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    if (! out.openedOk() || ! png.writeImageToStream (image, out))
        return 1;

    std::cout << "Wrote " << file.getFullPathName() << " (" << image.getWidth() << "x" << image.getHeight() << ")\n";
    return 0;
}

//==============================================================================
// Runs every juce::UnitTest registered in this executable.  Usage: ReeseTests [category]
int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return writeSnapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]),
                              argc > 3 ? juce::String (argv[3]).getIntValue() : 0,
                              argc > 4 ? juce::String (argv[4]).getIntValue() : 0);

    // print to stdout (the default goes to the debugger output on Windows, which CI can't see)
    struct ConsoleRunner : public juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override  { std::cout << message << std::endl; }
    };
    ConsoleRunner runner;
    runner.setAssertOnFailure (false);

    if (argc > 1)
        runner.runTestsInCategory (argv[1]);
    else
        runner.runAllTests();

    int failures = 0, passes = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        failures += runner.getResult (i)->failures;
        passes += runner.getResult (i)->passes;
    }

    std::cout << "\n" << passes << " checks passed, " << failures << " failed\n";
    return failures > 0 ? 1 : 0;
}
