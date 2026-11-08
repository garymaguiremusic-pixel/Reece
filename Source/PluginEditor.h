#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/CustomLookAndFeel.h"
#include "UI/VirusComponents.h"

//==============================================================================
/** The whole user interface, laid out at its base size. The editor below scales it to the window. */
class ReesePanel : public juce::Component,
                   private juce::Timer
{
public:
    static constexpr int kBaseWidth = 880, kBaseHeight = 504;

    explicit ReesePanel (ReeseAudioProcessor&);
    ~ReesePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct OscControls
    {
        juce::ComboBox waveBox;
        juce::TextButton octDown { "-12" }, octUp { "+12" };
        juce::TextButton phaseButton { "Phase" };   // off = free running
        juce::TextButton fmButton { "FM" };          // the other oscillator modulates this one
        VirusKnob fm { VirusKnob::Size::Small, "FM" };
        // shown instead of Semi / Fine / Voices / Detune while the wave is Noise
        juce::ComboBox noiseFilterBox;
        VirusKnob noiseCutoff { VirusKnob::Size::Small, "Cutoff" };
        VirusKnob noiseReso   { VirusKnob::Size::Small, "Reso" };
        LcdDisplay octLcd;
        VirusKnob semi   { VirusKnob::Size::Small, "Semi" };
        VirusKnob fine   { VirusKnob::Size::Small, "Fine" };
        VirusKnob voices { VirusKnob::Size::Small, "Voices" };
        VirusKnob detune { VirusKnob::Size::Small, "Detune" };
        VirusKnob pw     { VirusKnob::Size::Small, "PW" };
        VirusKnob phase  { VirusKnob::Size::Small, "Phase" };
        VirusKnob pan    { VirusKnob::Size::Small, "Pan" };
        VirusKnob level  { VirusKnob::Size::Small, "Level" };
        juce::Rectangle<float> panel;
    };

    void timerCallback() override;
    void addKnob (VirusKnob&, const juce::String& id, std::function<juce::String (float)> describe, const juce::String& tooltipName = {});
    void addToggle (juce::TextButton&, const juce::String& id, const juce::String& tooltip);
    void addCombo (juce::ComboBox&, const juce::String& id, const juce::StringArray& items, const juce::String& tooltip);
    void setupOsc (int index);
    void stepOctave (int osc, int delta);
    void updateActiveStates();
    void addSyncKnob (VirusKnob&, const juce::String& id, const juce::String& tooltipName);
    void createBackground();
    void drawWordmark (juce::Graphics&, juce::Rectangle<float> area);
    static void setActive (juce::Component&, bool active);
    float raw (const juce::String& id) const   { return processor.apvts.getRawParameterValue (id)->load(); }

    ReeseAudioProcessor& processor;
    CustomLookAndFeel lnf;
    juce::Image background;

    OscControls osc[rs::kNumOscs];

    // Filter
    juce::ComboBox filterTypeBox, slopeBox;
    VirusKnob cutoffKnob    { VirusKnob::Size::Large, "Cutoff" };
    VirusKnob resonanceKnob { VirusKnob::Size::Small, "Reso" };
    VirusKnob envAmtKnob    { VirusKnob::Size::Small, "Env Amt" };
    VirusKnob keyTrackKnob  { VirusKnob::Size::Small, "Key Track" };
    VirusKnob fAttack  { VirusKnob::Size::Small, "Attack" }, fDecay   { VirusKnob::Size::Small, "Decay" };
    VirusKnob fSustain { VirusKnob::Size::Small, "Sustain" }, fRelease { VirusKnob::Size::Small, "Release" };

    // Envelope tempo sync: note-value knobs that replace Attack / Decay / Release while Sync is on
    juce::TextButton fSyncButton { "Sync" }, aSyncButton { "Sync" };
    VirusKnob fAttackSync  { VirusKnob::Size::Small, "Attack" }, fDecaySync { VirusKnob::Size::Small, "Decay" };
    VirusKnob fReleaseSync { VirusKnob::Size::Small, "Release" };
    VirusKnob aAttackSync  { VirusKnob::Size::Small, "Attack" }, aDecaySync { VirusKnob::Size::Small, "Decay" };
    VirusKnob aReleaseSync { VirusKnob::Size::Small, "Release" };

    // Voice
    juce::ComboBox playModeBox;
    VirusKnob glideKnob { VirusKnob::Size::Small, "Glide" };

    // Low end / stereo / output
    VirusKnob monoBassKnob  { VirusKnob::Size::Small, "Mono Bass" };
    VirusKnob phaseLockKnob { VirusKnob::Size::Small, "Phase Lock" };
    VirusKnob spreadKnob    { VirusKnob::Size::Small, "Spread" };
    VirusKnob volumeKnob    { VirusKnob::Size::Small, "Volume" };

    // Saturation
    juce::TextButton satOnButton { "Saturation" };
    juce::ComboBox satTypeBox;
    VirusKnob satDrive { VirusKnob::Size::Small, "Drive" }, satTone  { VirusKnob::Size::Small, "Tone" };
    VirusKnob satMix   { VirusKnob::Size::Small, "Mix" },   satLevel { VirusKnob::Size::Small, "Level" };

    // Amp envelope
    VirusKnob aAttack  { VirusKnob::Size::Small, "Attack" }, aDecay   { VirusKnob::Size::Small, "Decay" };
    VirusKnob aSustain { VirusKnob::Size::Small, "Sustain" }, aRelease { VirusKnob::Size::Small, "Release" };

    juce::ComboBox presetBox;
    ScopeDisplay scope;
    HelpButton helpButton;
    HelpOverlay helpOverlay;
    juce::TooltipWindow tooltipWindow { this, 500 };

    // Attachments (declared after the controls so they are destroyed first)
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<KnobAttachment>> knobAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    juce::Rectangle<float> filterPanel, voicePanel, lowPanel, stereoPanel, satPanel, ampPanel;
    int lastShownProgram = -1;
    juce::String cutoffText;                  // shown in the Filter header

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReesePanel)
};

//==============================================================================
/** Resizable editor (fixed aspect ratio, 70 % .. 200 %); remembers its size. */
class ReeseAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ReeseAudioProcessorEditor (ReeseAudioProcessor&);
    void resized() override;

private:
    ReeseAudioProcessor& processor;
    ReesePanel panel;
    bool sizeRestored = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReeseAudioProcessorEditor)
};
