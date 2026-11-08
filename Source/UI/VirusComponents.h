#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    Rotary knob with Virus-style interaction (as in Virus Delay):
      - vertical drag and scroll wheel move in whole steps (1.0);
        hold Shift for 0.1 steps (64.1, 64.2 ...)
      - continuous knobs (filter Cutoff / Resonance) glide smoothly with no steps
      - double-click resets to the default value
      - small knobs show their label underneath, which swaps to the value
        (in real units) while hovering or dragging
    Drawing is delegated to CustomLookAndFeel::drawRotarySlider.
*/
class VirusKnob : public juce::Slider
{
public:
    enum class Size { Large, Small };

    VirusKnob (Size, const juce::String& labelText);

    Size getKnobSize() const noexcept           { return knobSize; }
    void setDefaultValue (double v) noexcept    { defaultValue = v; }
    void setContinuous (bool c) noexcept        { continuous = c; }
    void setBipolar (bool b)                    { getProperties().set ("bipolar", b); }

    std::function<void()> onGestureBegin, onGestureEnd;
    std::function<juce::String (float)> describeValue;   // real units for the label and tooltip
    std::function<juce::String (float)> tooltipValue;    // optional longer value text for the tooltip
    juce::String tooltipName;                            // tooltip title (defaults to the label)
    juce::String inactiveHint;                           // shown in the tooltip while disabled

    juce::String getTooltip() override;
    juce::String getValueText() const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void setValueWithGesture (double v);

    Size knobSize;
    juce::String label;
    double defaultValue = 0.0;
    float lastDragY = 0.0f;
    double dragValue = 0.0;          // un-snapped drag position
    bool dragShift = false, continuous = false;
    bool dragging = false, hovering = false;
};

//==============================================================================
/** Connects a VirusKnob to a parameter with proper host gesture handling. */
class KnobAttachment
{
public:
    KnobAttachment (juce::RangedAudioParameter&, VirusKnob&);
    ~KnobAttachment();

private:
    VirusKnob& knob;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Blue LCD readout, e.g. "CUTOFF   1.20 kHz". */
class LcdDisplay : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    void setContent (const juce::String& name, const juce::String& value);
    void setDimmed (bool shouldDim);
    void paint (juce::Graphics&) override;

private:
    juce::String nameText, valueText;
    bool dimmed = false;
};

//==============================================================================
/** Output oscilloscope (L and R) in the bottom strip, with a text summary at the right. */
class ScopeDisplay : public juce::Component
{
public:
    /** ring buffers of size 'size' (power of two), and the processor's current write index. */
    void update (const float* left, const float* right, int size, int writeIndex);
    void setInfo (const juce::StringArray& lines);
    void paint (juce::Graphics&) override;

private:
    static constexpr int kPoints = 512;
    float l[kPoints] {}, r[kPoints] {};
    juce::StringArray info;
};

//==============================================================================
/** Round "?" button in the bottom bar. */
class HelpButton : public juce::Button
{
public:
    HelpButton() : juce::Button ("help") {}
    void paintButton (juce::Graphics&, bool isHighlighted, bool isDown) override;
};

/** Quick reference shown over the editor; click anywhere to close. */
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
