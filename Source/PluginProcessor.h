#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/SynthEngine.h"
#include "DSP/Saturation.h"

//==============================================================================
class ReeseAudioProcessor : public juce::AudioProcessor
{
public:
    ReeseAudioProcessor();
    ~ReeseAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    const juce::String getName() const override          { return "Reese"; }
    bool acceptsMidi() const override                    { return true; }
    bool producesMidi() const override                   { return false; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override                     { return currentProgram.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    /** Randomises the LFO and/or Saturation sections, as chosen by their Randomise buttons (message thread). */
    void randomise();

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    /** Read-only state for the editor (written on the audio thread). */
    struct GuiState
    {
        static constexpr int kScopeSize = 4096;
        float scopeL[kScopeSize] {}, scopeR[kScopeSize] {};
        std::atomic<int> scopeWrite { 0 };
        std::atomic<int> activeVoices { 0 };
        std::atomic<float> peak { 0.0f };
        std::atomic<int> editorWidth { 0 };    // remembered editor size (0 = default)
        std::atomic<float> bpm { (float) rs::kDefaultBpm };
        std::atomic<float> lfoPhase { 0.0f }, lfoValue { 0.0f };
    };
    GuiState gui;

private:
    rs::EngineSettings makeEngineSettings() const;
    rs::SaturationSettings makeSaturationSettings() const;
    void processOutput (juce::AudioBuffer<float>&) noexcept;

    float raw (const juce::String& id) const   { return apvts.getRawParameterValue (id)->load(); }

    rs::SynthEngine engine;
    rs::Saturation saturation;
    rs::LR4Crossover monoXoverL, monoXoverR;
    juce::SmoothedValue<float> spreadSmooth, volumeSmooth, monoHzSmooth;
    double currentSampleRate = 44100.0;
    double bpm = rs::kDefaultBpm;

    std::atomic<int> currentProgram { 0 };
    juce::Random randomiser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReeseAudioProcessor)
};
