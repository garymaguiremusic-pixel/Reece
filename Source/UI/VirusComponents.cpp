#include "VirusComponents.h"
#include "CustomLookAndFeel.h"

using namespace juce;

namespace
{
    String formatNumber (double v)
    {
        const double r = std::round (v * 10.0) / 10.0;
        return std::abs (r - std::round (r)) < 0.05 ? String (roundToInt (r)) : String (r, 1);
    }
}

//==============================================================================
// VirusKnob
//==============================================================================
VirusKnob::VirusKnob (Size s, const String& labelText)
    : Slider (Slider::RotaryVerticalDrag, Slider::NoTextBox), knobSize (s), label (labelText)
{
    setRange (0.0, 100.0, 0.0);
    setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    setName (labelText);
}

String VirusKnob::getValueText() const
{
    const auto v = (float) getValue();
    return describeValue ? describeValue (v) : formatNumber (v);
}

String VirusKnob::getTooltip()
{
    String text = (tooltipName.isNotEmpty() ? tooltipName : label).toUpperCase() + "  " + getValueText();
    if (! isEnabled() && inactiveHint.isNotEmpty())
        text << "\n" << inactiveHint;
    return text;
}

void VirusKnob::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const float pos = (float) valueToProportionOfLength (getValue());
    const auto rotary = getRotaryParameters();

    Rectangle<float> knobArea;
    if (knobSize == Size::Large)
    {
        knobArea = bounds;
    }
    else
    {
        auto labelArea = bounds.removeFromBottom (14.0f);
        const float side = std::min (bounds.getWidth(), bounds.getHeight());
        knobArea = bounds.withSizeKeepingCentre (side, side);

        if ((hovering || dragging) && isEnabled())
        {
            g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
            g.setColour (VirusColours::blue);
            g.drawFittedText (getValueText(), labelArea.toNearestInt(), Justification::centred, 1, 0.7f);
        }
        else
        {
            CustomLookAndFeel::drawSmallLabel (g, label, labelArea);
        }
    }

    getLookAndFeel().drawRotarySlider (g, (int) knobArea.getX(), (int) knobArea.getY(),
                                       (int) knobArea.getWidth(), (int) knobArea.getHeight(),
                                       pos, rotary.startAngleRadians, rotary.endAngleRadians, *this);
}

void VirusKnob::setValueWithGesture (double v)
{
    if (onGestureBegin) onGestureBegin();
    setValue (jlimit (getMinimum(), getMaximum(), v), sendNotificationSync);
    if (onGestureEnd) onGestureEnd();
}

void VirusKnob::mouseDown (const MouseEvent& e)
{
    if (! isEnabled())
        return;

    dragging = true;
    lastDragY = e.position.y;
    dragValue = getValue();
    dragShift = e.mods.isShiftDown();
    if (onGestureBegin) onGestureBegin();
    repaint();
}

void VirusKnob::mouseDrag (const MouseEvent& e)
{
    if (! isEnabled() || ! dragging)
        return;

    const bool shift = e.mods.isShiftDown();
    if (shift != dragShift)
    {
        // pressing or releasing Shift mid-drag continues from the current value
        dragShift = shift;
        dragValue = getValue();
    }

    const double pixels = (double) (lastDragY - e.position.y);
    lastDragY = e.position.y;
    const double range = getMaximum() - getMinimum();

    if (continuous)
    {
        // smooth, no steps: full range in 250 pixels, Shift = 8x finer
        dragValue = jlimit (getMinimum(), getMaximum(), dragValue + pixels * range / (shift ? 2000.0 : 250.0));
        if (std::abs (dragValue - getValue()) > 1.0e-9)
            setValue (dragValue, sendNotificationSync);
        return;
    }

    // whole steps normally (full range in 200 pixels), 0.1 steps with Shift (0.1 every 2 pixels)
    dragValue += shift ? pixels * 0.05 : pixels * range / 200.0;
    dragValue = jlimit (getMinimum(), getMaximum(), dragValue);

    const double snapped = shift ? std::round (dragValue * 10.0) / 10.0 : std::round (dragValue);
    if (std::abs (snapped - getValue()) > 1.0e-9)
        setValue (jlimit (getMinimum(), getMaximum(), snapped), sendNotificationSync);
}

void VirusKnob::mouseUp (const MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    if (onGestureEnd) onGestureEnd();
    repaint();
}

void VirusKnob::mouseDoubleClick (const MouseEvent&)
{
    if (isEnabled())
        setValueWithGesture (defaultValue);
}

void VirusKnob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (! isEnabled())
        return;

    const float dir = (wheel.isReversed ? -1.0f : 1.0f) * (std::abs (wheel.deltaY) > 0.0f ? wheel.deltaY : wheel.deltaX);
    if (std::abs (dir) < 1.0e-6f)
        return;

    // One notch = 1.0, or 0.1 with Shift.
    const bool shift = e.mods.isShiftDown();
    const double step = shift ? 0.1 : 1.0;
    double target = getValue() + (dir > 0.0f ? step : -step);
    if (! continuous)
        target = shift ? std::round (target * 10.0) / 10.0 : std::round (target);
    setValueWithGesture (target);
}

void VirusKnob::mouseEnter (const MouseEvent&) { hovering = true;  repaint(); }
void VirusKnob::mouseExit (const MouseEvent&)  { hovering = false; repaint(); }

//==============================================================================
// KnobAttachment
//==============================================================================
KnobAttachment::KnobAttachment (RangedAudioParameter& p, VirusKnob& k)
    : knob (k),
      attachment (p, [this] (float v) { knob.setValue (v, dontSendNotification); knob.repaint(); })
{
    const auto range = p.getNormalisableRange();
    knob.setRange (range.start, range.end, 0.0);
    knob.setDefaultValue (p.convertFrom0to1 (p.getDefaultValue()));

    knob.onGestureBegin = [this] { attachment.beginGesture(); };
    knob.onGestureEnd   = [this] { attachment.endGesture(); };
    knob.onValueChange  = [this] { attachment.setValueAsPartOfGesture ((float) knob.getValue()); };

    attachment.sendInitialUpdate();
}

KnobAttachment::~KnobAttachment()
{
    knob.onGestureBegin = nullptr;
    knob.onGestureEnd = nullptr;
    knob.onValueChange = nullptr;
}

//==============================================================================
// LcdDisplay
//==============================================================================
void LcdDisplay::setContent (const String& name, const String& value)
{
    if (name != nameText || value != valueText)
    {
        nameText = name;
        valueText = value;
        repaint();
    }
}

void LcdDisplay::setDimmed (bool shouldDim)
{
    if (dimmed != shouldDim)
    {
        dimmed = shouldDim;
        repaint();
    }
}

void LcdDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    CustomLookAndFeel::drawLcd (g, b, dimmed);

    const auto textColour = dimmed ? Colour (0xff8a9097) : Colours::white;
    auto textArea = b.reduced (6.0f, 0.0f);
    const auto just = nameText.isEmpty() ? Justification::centred : Justification::centredRight;

    g.setFont (CustomLookAndFeel::uiFont (11.0f, true));
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawText (nameText, textArea.translated (0.0f, 1.0f), Justification::centredLeft);
    g.drawText (valueText, textArea.translated (0.0f, 1.0f), just);
    g.setColour (textColour);
    g.drawText (nameText, textArea, Justification::centredLeft);
    g.drawText (valueText, textArea, just);
}

//==============================================================================
// ScopeDisplay
//==============================================================================
void ScopeDisplay::update (const float* left, const float* right, int size, int writeIndex)
{
    // Shows the last ~2048 samples (every 4th one). Starts on a rising zero crossing of
    // the mid signal, so the trace stands still on a held note.
    constexpr int stride = 4;
    const int span = kPoints * stride;
    const int mask = size - 1;
    const int searchSpan = size - span - 2;
    int start = (writeIndex - span - 1) & mask;
    for (int back = 0; back < searchSpan; ++back)
    {
        const int i = (writeIndex - span - 1 - back) & mask;
        const int prev = (i - 1) & mask;
        const float m = left[i] + right[i], mp = left[prev] + right[prev];
        if (mp < 0.0f && m >= 0.0f)
        {
            start = i;
            break;
        }
    }

    for (int k = 0; k < kPoints; ++k)
    {
        const int i = (start + k * stride) & mask;
        l[k] = left[i];
        r[k] = right[i];
    }
    repaint();
}

void ScopeDisplay::setInfo (const StringArray& lines)
{
    if (lines != info)
    {
        info = lines;
        repaint();
    }
}

void ScopeDisplay::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    CustomLookAndFeel::drawInsetBox (g, b, 4.0f);

    auto inner = b.reduced (6.0f, 4.0f);
    auto text = inner.removeFromRight (260.0f);
    auto plot = inner.reduced (2.0f, 1.0f);

    g.setColour (Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) i / 8.0f), plot.getY(), plot.getBottom());

    // autoscale gently so quiet patches are still visible
    float peak = 0.05f;
    for (int k = 0; k < kPoints; ++k)
        peak = jmax (peak, std::abs (l[k]), std::abs (r[k]));
    const float scale = plot.getHeight() * 0.48f / jmax (0.25f, peak);

    auto trace = [&] (const float* data)
    {
        Path p;
        for (int k = 0; k < kPoints; ++k)
        {
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) (kPoints - 1);
            const float half = plot.getHeight() * 0.5f;
            const float y = plot.getCentreY() - jlimit (-half, half, data[k] * scale);
            if (k == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        return p;
    };

    const auto pr = trace (r), pl = trace (l);
    g.setColour (Colour (0xffc9e6ff).withAlpha (0.45f));
    g.strokePath (pr, PathStrokeType (1.0f));
    g.setColour (VirusColours::blue.withAlpha (0.35f));
    g.strokePath (pl, PathStrokeType (3.0f));
    g.setColour (VirusColours::blue);
    g.strokePath (pl, PathStrokeType (1.3f));

    // labels
    g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
    g.setColour (VirusColours::labelText);
    g.drawText ("L", plot.getX() + 2.0f, plot.getY(), 10.0f, 10.0f, Justification::centredLeft);
    g.setColour (Colour (0xffc9e6ff).withAlpha (0.7f));
    g.drawText ("R", plot.getX() + 12.0f, plot.getY(), 10.0f, 10.0f, Justification::centredLeft);

    // summary text
    g.setColour (VirusColours::bevelDark);
    g.drawVerticalLine ((int) text.getX() - 3, b.getY() + 4.0f, b.getBottom() - 4.0f);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawVerticalLine ((int) text.getX() - 2, b.getY() + 4.0f, b.getBottom() - 4.0f);

    auto t = text.withTrimmedLeft (8.0f);
    const float lineH = t.getHeight() / (float) jmax (1, info.size());
    for (int i = 0; i < info.size(); ++i)
    {
        g.setFont (CustomLookAndFeel::uiFont (i == 0 ? 11.0f : 10.5f, i == 0));
        g.setColour (i == 0 ? Colours::white.withAlpha (0.85f) : VirusColours::labelText);
        g.drawFittedText (info[i], t.removeFromTop (lineH).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}

//==============================================================================
// HelpButton / HelpOverlay
//==============================================================================
void HelpButton::paintButton (Graphics& g, bool isHighlighted, bool isDown)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillEllipse (b.translated (0.0f, 1.0f));
    g.setGradientFill (ColourGradient (isHighlighted ? Colour (0xff8a9096) : Colour (0xff6f757b), 0.0f, b.getY(),
                                       Colour (0xff35393d), 0.0f, b.getBottom(), false));
    g.fillEllipse (isDown ? b.reduced (0.5f) : b);
    g.setColour (VirusColours::bevelDark);
    g.drawEllipse (b, 1.0f);
    g.setColour (Colours::white);
    g.setFont (CustomLookAndFeel::uiFont (12.0f, true));
    g.drawText ("?", b, Justification::centred);
}

void HelpOverlay::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));

    auto box = getLocalBounds().toFloat().reduced (60.0f, 30.0f);
    CustomLookAndFeel::drawPanel (g, box, "Reese - Quick Help");

    const char* lines[] =
    {
        "OSC 1 / OSC 2:  Saw, Ramp, Square, Pulse (PW), Triangle, Sine, Saw + Square, Noise",
        "-12 / +12:  shift the oscillator down or up an octave.  SEMI / FINE:  tuning (FINE detunes in cents)",
        "VOICES / DETUNE:  unison stack (1 - 8) spread across +- DETUNE cents and across the stereo field",
        "PHASE:  where the oscillator starts when it is retriggered.  PAN / LEVEL:  placement and volume",
        "RETRIG:  restart the oscillators on every new note, so each note starts the same way",
        "MONO BASS:  everything below this frequency is summed to mono (Off - 200 Hz)",
        "PHASE LOCK:  below this frequency the oscillators are retriggered and held in phase, so the sub",
        "        never beats or cancels; above it the detuned Reese movement stays (Off - 200 Hz)",
        "FILTER:  analog-style Low / Band / High Pass, 12, 24 or 36 dB/oct. Cutoff and Resonance move smoothly",
        "SPREAD:  stereo width. Left = mono, centre = 100 %, right = 200 %",
        "SATURATION:  the KickDrum saturation (Soft, Tape, Tube, Hard Clip, Foldback, Fuzz), 4x oversampled",
        "MODE:  Mono (retrigger), Legato (glide without retrigger) or Poly (8 notes).  GLIDE:  portamento time",
        "Knobs move in steps of 1; hold Shift for 0.1 steps. Scroll wheel too. Double-click = default.",
        "Drag the bottom-right corner to resize the window.",
        "",
        "Click anywhere to close."
    };

    auto text = box.reduced (14.0f, 0.0f).withTrimmedTop (28.0f);
    g.setFont (CustomLookAndFeel::uiFont (11.5f));
    for (auto* line : lines)
    {
        g.setColour (String (line).startsWith ("Click") ? VirusColours::blue : Colour (0xffdfe3e6));
        g.drawFittedText (line, text.removeFromTop (22.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    }
}
