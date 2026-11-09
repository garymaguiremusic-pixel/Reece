#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;
using namespace rs;

namespace
{
    constexpr int kWidth = ReesePanel::kBaseWidth, kHeight = ReesePanel::kBaseHeight;
    constexpr int kTopStripH = 34, kBottomBarY = 472;
}

//==============================================================================
ReesePanel::ReesePanel (ReeseAudioProcessor& p)
    : processor (p)
{
    setLookAndFeel (&lnf);
    setOpaque (true);
    setSize (kWidth, kHeight);
    createBackground();

    // Oscillators -----------------------------------------------------------------------
    for (int o = 0; o < kNumOscs; ++o)
        setupOsc (o);

    // Filter ------------------------------------------------------------------------------
    addCombo (filterTypeBox, ParamIDs::fltType, getFilterTypeNames(), "Filter type");
    addCombo (slopeBox, ParamIDs::fltSlope, getSlopeNames(), "Filter slope");
    addKnob (cutoffKnob, ParamIDs::cutoff, formatCutoff, "Filter Cutoff");
    addKnob (resonanceKnob, ParamIDs::resonance, formatPercent, "Filter Resonance");
    addKnob (envAmtKnob, ParamIDs::fltEnv, formatSemis, "Filter Envelope Amount");
    addKnob (keyTrackKnob, ParamIDs::keyTrack, formatPercent, "Filter Key Tracking");
    addKnob (fAttack, ParamIDs::fAttack, formatEnvTime, "Filter Attack");
    addKnob (fDecay, ParamIDs::fDecay, formatEnvTime, "Filter Decay");
    addKnob (fSustain, ParamIDs::fSustain, formatPercent, "Filter Sustain");
    addKnob (fRelease, ParamIDs::fRelease, formatEnvTime, "Filter Release");
    cutoffKnob.setContinuous (true);       // the filter moves smoothly, with no steps
    resonanceKnob.setContinuous (true);

    // Voice ----------------------------------------------------------------------------------
    addCombo (playModeBox, ParamIDs::playMode, getPlayModeNames(), "Mono (retrigger), Legato (glide, no retrigger) or Poly (8 notes)");
    addKnob (glideKnob, ParamIDs::glide, formatGlide, "Glide Time");

    // Low end / stereo / output ------------------------------------------------------------
    addKnob (monoBassKnob, ParamIDs::monoBass, formatLowHz, "Mono Bass (below this, the signal is mono)");
    addKnob (phaseLockKnob, ParamIDs::phaseLock, formatLowHz, "Phase Lock (below this, the oscillators are retriggered and kept in phase)");
    addKnob (spreadKnob, ParamIDs::spread, formatSpread, "Stereo Spread (left = mono, right = 200 %)");
    addKnob (volumeKnob, ParamIDs::volume, formatVolume, "Volume");

    // Saturation ----------------------------------------------------------------------------
    addToggle (satOnButton, ParamIDs::satOn, "Saturation on/off (the KickDrum saturation, 4x oversampled)");
    addCombo (satTypeBox, ParamIDs::satType, getSatTypeNames(), "Saturation type");
    addKnob (satDrive, ParamIDs::satDrive, formatDb, "Saturation Drive");
    addKnob (satTone, ParamIDs::satTone, formatPercent, "Saturation Tone");
    addKnob (satMix, ParamIDs::satMix, formatPercent, "Saturation Mix");
    addKnob (satLevel, ParamIDs::satLevel, formatDb, "Saturation Level");

    // Amp -------------------------------------------------------------------------------------
    addKnob (aAttack, ParamIDs::aAttack, formatEnvTime, "Amp Attack");
    addKnob (aDecay, ParamIDs::aDecay, formatEnvTime, "Amp Decay");
    addKnob (aSustain, ParamIDs::aSustain, formatPercent, "Amp Sustain");
    addKnob (aRelease, ParamIDs::aRelease, formatEnvTime, "Amp Release");

    // Envelope sync ------------------------------------------------------------------------------
    addToggle (fSyncButton, ParamIDs::fSync, "Sync the filter envelope's Attack, Decay and Release to the tempo (1/512 to 1/1)");
    addToggle (aSyncButton, ParamIDs::aSync, "Sync the amp envelope's Attack, Decay and Release to the tempo (1/512 to 1/1)");
    addSyncKnob (fAttackSync,  ParamIDs::fAttackSync,  "Filter Attack");
    addSyncKnob (fDecaySync,   ParamIDs::fDecaySync,   "Filter Decay");
    addSyncKnob (fReleaseSync, ParamIDs::fReleaseSync, "Filter Release");
    addSyncKnob (aAttackSync,  ParamIDs::aAttackSync,  "Amp Attack");
    addSyncKnob (aDecaySync,   ParamIDs::aDecaySync,   "Amp Decay");
    addSyncKnob (aReleaseSync, ParamIDs::aReleaseSync, "Amp Release");

    // Envelope times and Glide: the knob shows its own 0..100 number (1.0 steps, Shift = 0.1),
    // and the tooltip adds the real time.
    for (VirusKnob* k : { &fAttack, &fDecay, &fRelease, &aAttack, &aDecay, &aRelease })
    {
        k->describeValue = [] (float v) { return trimmed (v); };
        k->tooltipValue = [] (float v) { return trimmed (v) + "   (" + formatEnvTime (v) + ")"; };
    }
    glideKnob.describeValue = [] (float v) { return v <= 0.0f ? String ("Off") : trimmed (v); };
    glideKnob.tooltipValue = [] (float v) { return v <= 0.0f ? String ("Off") : trimmed (v) + "   (" + formatGlide (v) + ")"; };

    // Presets -----------------------------------------------------------------------------------
    const auto& presets = getFactoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
        presetBox.addItem (String ((int) i + 1).paddedLeft ('0', 2) + "  " + presets[i].name, (int) i + 1);
    presetBox.setTooltip ("Factory presets");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram (index);
        lastShownProgram = index;
    };
    addAndMakeVisible (presetBox);

    addAndMakeVisible (scope);
    helpButton.setTooltip ("Help");
    helpButton.onClick = [this] { helpOverlay.setVisible (true); helpOverlay.toFront (false); };
    addAndMakeVisible (helpButton);
    addChildComponent (helpOverlay);

    timerCallback();
    startTimerHz (30);
}

ReesePanel::~ReesePanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ReesePanel::addKnob (VirusKnob& knob, const String& id, std::function<String (float)> describe, const String& tooltipName)
{
    knob.describeValue = std::move (describe);
    knob.tooltipName = tooltipName;
    knobAttachments.push_back (std::make_unique<KnobAttachment> (*processor.apvts.getParameter (id), knob));
    addAndMakeVisible (knob);
}

void ReesePanel::addSyncKnob (VirusKnob& knob, const String& id, const String& tooltipName)
{
    // one knob step = one note value; the tooltip adds the time at the current tempo
    addKnob (knob, id, [] (float v) { return getSyncDivisionNames()[jlimit (0, kNumSyncDivisions - 1, roundToInt (v))]; },
             tooltipName + " (synced)");
    knob.tooltipValue = [this] (float v)
    {
        const int index = jlimit (0, kNumSyncDivisions - 1, roundToInt (v));
        return getSyncDivisionNames()[index] + "   (" + formatMs (syncDivisionToMs (index, processor.gui.bpm.load())) + ")";
    };
    knob.setVisible (false);
}

void ReesePanel::addToggle (TextButton& button, const String& id, const String& tooltip)
{
    button.setClickingTogglesState (true);
    button.setTooltip (tooltip);
    buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.apvts, id, button));
    addAndMakeVisible (button);
}

void ReesePanel::addCombo (ComboBox& box, const String& id, const StringArray& items, const String& tooltip)
{
    box.addItemList (items, 1);
    box.setTooltip (tooltip);
    box.setJustificationType (Justification::centredLeft);
    comboAttachments.push_back (std::make_unique<ComboAttachment> (processor.apvts, id, box));
    addAndMakeVisible (box);
}

void ReesePanel::setupOsc (int o)
{
    auto& c = osc[o];
    const String n = "Osc " + String (o + 1) + " ";

    addCombo (c.waveBox, oscParam (o, ParamIDs::wave), getWaveNames(), n + "waveform");
    addKnob (c.semi,   oscParam (o, ParamIDs::semi),   formatSemis,   n + "Semitones");
    addKnob (c.fine,   oscParam (o, ParamIDs::fine),   formatCents,   n + "Fine Detune");
    addKnob (c.voices, oscParam (o, ParamIDs::voices), formatVoices,  n + "Unison Voices");
    addKnob (c.detune, oscParam (o, ParamIDs::detune), [] (float v) { return "+-" + trimmed (v) + " ct"; }, n + "Unison Detune");
    addKnob (c.pw,     oscParam (o, ParamIDs::pw),     formatPercent, n + "Pulse Width");
    addKnob (c.phase,  oscParam (o, ParamIDs::phase),  formatDegrees, n + "Start Phase");
    addKnob (c.pan,    oscParam (o, ParamIDs::pan),    formatPan,     n + "Pan");
    addKnob (c.level,  oscParam (o, ParamIDs::level),  formatPercent, n + "Level");

    addToggle (c.fmButton, oscParam (o, ParamIDs::fmOn),
               n + "FM: Osc " + String (2 - o) + " modulates this oscillator (it works even with its Level at 0)");
    addKnob (c.fm, oscParam (o, ParamIDs::fmAmount), formatPercent, n + "FM Amount (from Osc " + String (2 - o) + ")");
    c.fm.inactiveHint = "Turn on this oscillator's FM button";
    addCombo (c.noiseFilterBox, oscParam (o, ParamIDs::noiseFilter), getNoiseFilterNames(), n + "noise filter type (Noise only)");
    addKnob (c.noiseCutoff, oscParam (o, ParamIDs::noiseCutoff), formatCutoff, n + "Noise Filter Cutoff");
    addKnob (c.noiseReso, oscParam (o, ParamIDs::noiseReso), formatPercent, n + "Noise Filter Resonance");
    c.noiseCutoff.setContinuous (true);
    c.noiseReso.setContinuous (true);

    c.pw.inactiveHint = "Pulse width is used by the Pulse wave";
    c.detune.inactiveHint = "Set VOICES above 1 to spread a unison stack";
    c.phase.inactiveHint = "Turn on this oscillator's PHASE button (or PHASE LOCK) to use the start phase";
    addToggle (c.phaseButton, oscParam (o, ParamIDs::retrig),
               n + "Phase: off = free running. On = restart at the PHASE knob on every new note");

    c.octDown.setTooltip (n + "down one octave (-12 semitones)");
    c.octUp.setTooltip (n + "up one octave (+12 semitones)");
    c.octDown.onClick = [this, o] { stepOctave (o, -1); };
    c.octUp.onClick   = [this, o] { stepOctave (o, +1); };
    c.octLcd.setTooltip (n + "octave transpose");
    addAndMakeVisible (c.octDown);
    addAndMakeVisible (c.octUp);
    addAndMakeVisible (c.octLcd);
}

void ReesePanel::stepOctave (int o, int delta)
{
    if (auto* param = processor.apvts.getParameter (oscParam (o, ParamIDs::octave)))
    {
        const auto range = param->getNormalisableRange();
        const float current = param->convertFrom0to1 (param->getValue());
        const float next = jlimit (range.start, range.end, std::round (current) + (float) delta);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (next));
        param->endChangeGesture();
    }
}

//==============================================================================
void ReesePanel::resized()
{
    // top strip
    presetBox.setBounds (660, 7, 212, 21);

    // oscillators
    for (int o = 0; o < kNumOscs; ++o)
    {
        auto& c = osc[o];
        const int x = 8 + o * 286;
        c.panel = { (float) x, 40.0f, 280.0f, 250.0f };

        c.phaseButton.setBounds (x + 226, 42, 46, 14);
        c.fmButton.setBounds (x + 184, 42, 38, 14);
        c.waveBox.setBounds (x + 12, 78, 124, 22);
        c.octDown.setBounds (x + 144, 78, 32, 22);
        c.octLcd.setBounds  (x + 179, 78, 58, 22);
        c.octUp.setBounds   (x + 240, 78, 32, 22);

        // Noise: filter type, cutoff and resonance take the place of Semi / Fine / Voices / Detune
        c.noiseFilterBox.setBounds (x + 12, 134, 120, 22);
        c.noiseCutoff.setBounds (x + 140, 112, 64, 72);
        c.noiseReso.setBounds (x + 204, 112, 64, 72);

        VirusKnob* row1[] = { &c.semi, &c.fine, &c.voices, &c.detune, &c.fm };
        VirusKnob* row2[] = { &c.pw, &c.phase, &c.pan, &c.level };
        for (int i = 0; i < 5; ++i)
            row1[i]->setBounds (x + 12 + i * 51, 112, 51, 72);
        for (int i = 0; i < 4; ++i)
            row2[i]->setBounds (x + 12 + i * 64, 204, 64, 72);
    }

    // filter
    filterPanel = { 580.0f, 40.0f, 292.0f, 250.0f };
    filterTypeBox.setBounds (592, 78, 130, 22);
    slopeBox.setBounds (730, 78, 130, 22);
    cutoffKnob.setBounds (592, 106, 96, 96);
    resonanceKnob.setBounds (698, 118, 56, 72);
    envAmtKnob.setBounds (754, 118, 56, 72);
    keyTrackKnob.setBounds (810, 118, 56, 72);
    {
        VirusKnob* env[] = { &fAttack, &fDecay, &fSustain, &fRelease };
        VirusKnob* sync[] = { &fAttackSync, &fDecaySync, nullptr, &fReleaseSync };
        for (int i = 0; i < 4; ++i)
        {
            env[i]->setBounds (592 + i * 68, 216, 64, 70);
            if (sync[i] != nullptr)
                sync[i]->setBounds (env[i]->getBounds());
        }
    }
    fSyncButton.setBounds (818, 201, 42, 15);

    // second row
    voicePanel  = { 8.0f,   296.0f, 128.0f, 108.0f };
    lowPanel    = { 142.0f, 296.0f, 136.0f, 108.0f };
    stereoPanel = { 284.0f, 296.0f, 128.0f, 108.0f };
    satPanel    = { 418.0f, 296.0f, 258.0f, 108.0f };
    ampPanel    = { 682.0f, 296.0f, 190.0f, 108.0f };

    playModeBox.setBounds (16, 346, 68, 21);
    glideKnob.setBounds (86, 320, 48, 72);

    monoBassKnob.setBounds (148, 320, 64, 72);
    phaseLockKnob.setBounds (212, 320, 64, 72);

    spreadKnob.setBounds (288, 320, 60, 72);
    volumeKnob.setBounds (348, 320, 60, 72);

    satOnButton.setBounds (426, 330, 78, 21);
    satTypeBox.setBounds (426, 362, 78, 21);
    {
        VirusKnob* k[] = { &satDrive, &satTone, &satMix, &satLevel };
        for (int i = 0; i < 4; ++i)
            k[i]->setBounds (508 + i * 41, 320, 41, 72);
    }
    {
        VirusKnob* k[] = { &aAttack, &aDecay, &aSustain, &aRelease };
        VirusKnob* sync[] = { &aAttackSync, &aDecaySync, nullptr, &aReleaseSync };
        for (int i = 0; i < 4; ++i)
        {
            k[i]->setBounds (688 + i * 45, 320, 45, 72);
            if (sync[i] != nullptr)
                sync[i]->setBounds (k[i]->getBounds());
        }
    }
    aSyncButton.setBounds (826, 298, 42, 14);

    scope.setBounds (8, 410, 864, 56);
    helpButton.setBounds (kWidth - 26, kBottomBarY + 6, 20, 20);
    helpOverlay.setBounds (getLocalBounds());
}

void ReesePanel::setActive (Component& c, bool active)
{
    if (c.isEnabled() != active)
    {
        c.setEnabled (active);
        c.setAlpha (active ? 1.0f : 0.4f);
    }
}

void ReesePanel::updateActiveStates()
{
    const bool lock = raw (ParamIDs::phaseLock) >= 1.0f;

    for (int o = 0; o < kNumOscs; ++o)
    {
        auto& c = osc[o];
        const int wave = (int) raw (oscParam (o, ParamIDs::wave));
        const bool noise = wave == WaveNoise;
        setActive (c.pw, wave == WavePulse);
        setActive (c.detune, roundToInt (raw (oscParam (o, ParamIDs::voices))) > 1);
        const bool retrig = raw (oscParam (o, ParamIDs::retrig)) > 0.5f;
        // Noise: PHASE locks the noise pattern, and the Phase knob picks which pattern
        setActive (c.phase, noise ? retrig : (retrig || lock));
        c.phase.inactiveHint = noise ? "Turn on this oscillator's PHASE button to lock the noise pattern"
                                     : "Turn on this oscillator's PHASE button (or PHASE LOCK) to use the start phase";
        for (Component* tonal : { (Component*) &c.semi, (Component*) &c.fine, (Component*) &c.voices, (Component*) &c.detune, (Component*) &c.fm })
            tonal->setVisible (! noise);
        setActive (c.fmButton, ! noise);
        setActive (c.fm, raw (oscParam (o, ParamIDs::fmOn)) > 0.5f);
        for (Component* nc : { (Component*) &c.noiseFilterBox, (Component*) &c.noiseCutoff, (Component*) &c.noiseReso })
            nc->setVisible (noise);
        c.phase.describeValue = noise ? std::function<String (float)> ([] (float v) { return "Pattern " + String (roundToInt (v)); })
                                      : std::function<String (float)> (formatDegrees);
        setActive (c.octDown, ! noise);
        setActive (c.octUp, ! noise);
        c.octLcd.setDimmed (noise);
    }

    // Sync swaps the free-time knobs for note-value knobs in the same places
    const bool fSync = raw (ParamIDs::fSync) > 0.5f, aSync = raw (ParamIDs::aSync) > 0.5f;
    for (auto [freeKnob, syncKnob, synced] : { std::tuple<VirusKnob*, VirusKnob*, bool> { &fAttack, &fAttackSync, fSync },
                                               { &fDecay, &fDecaySync, fSync }, { &fRelease, &fReleaseSync, fSync },
                                               { &aAttack, &aAttackSync, aSync }, { &aDecay, &aDecaySync, aSync },
                                               { &aRelease, &aReleaseSync, aSync } })
    {
        freeKnob->setVisible (! synced);
        syncKnob->setVisible (synced);
    }

    setActive (glideKnob, (int) raw (ParamIDs::playMode) != ModePoly);
    glideKnob.inactiveHint = "Glide works in Mono and Legato modes";

    const bool satOn = raw (ParamIDs::satOn) > 0.5f;
    for (Component* c : { (Component*) &satTypeBox, (Component*) &satDrive, (Component*) &satTone,
                          (Component*) &satMix, (Component*) &satLevel })
        setActive (*c, satOn);
}

//==============================================================================
void ReesePanel::createBackground()
{
    // Rendered at 2x so it stays sharp when the window is enlarged.
    background = Image (Image::RGB, kWidth * 2, kHeight * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));

    g.setGradientFill (ColourGradient (Colour (0xff3b3e42), 0.0f, 0.0f, Colour (0xff1e2023), 0.0f, (float) kHeight, false));
    g.fillAll();

    // brushed metal: faint full-width lines plus random short streaks
    Random rng (0x7a11);
    for (int y = 0; y < kHeight; ++y)
    {
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (rng.nextFloat() * 0.035f));
        g.fillRect (0, y, kWidth, 1);
    }
    for (int i = 0; i < 2600; ++i)
    {
        const int x = rng.nextInt (kWidth), y = rng.nextInt (kHeight), len = 20 + rng.nextInt (140);
        g.setColour ((rng.nextBool() ? Colours::white : Colours::black).withAlpha (0.015f + rng.nextFloat() * 0.03f));
        g.fillRect (x, y, len, 1);
    }

    // soft vignette
    ColourGradient vignette (Colours::transparentBlack, kWidth * 0.5f, kHeight * 0.45f,
                             Colours::black.withAlpha (0.35f), 0.0f, 0.0f, true);
    g.setGradientFill (vignette);
    g.fillAll();
}

void ReesePanel::drawWordmark (Graphics& g, Rectangle<float> area)
{
    // Same family wordmark as Virus Delay: chrome first word, blue second word, swoosh.
    const float baseline = area.getBottom() - 7.0f;

    GlyphArrangement main;
    main.addLineOfText (Font (FontOptions (27.0f, Font::bold | Font::italic)), "reese", area.getX(), baseline);
    Path mainPath;
    main.createPath (mainPath);
    const auto mainBounds = mainPath.getBounds();

    g.setColour (Colours::black.withAlpha (0.6f));
    g.fillPath (mainPath, AffineTransform::translation (1.0f, 1.5f));
    ColourGradient chrome (Colour (0xffffffff), 0.0f, mainBounds.getY(), Colour (0xff8c949c), 0.0f, mainBounds.getBottom(), false);
    chrome.addColour (0.5, Colour (0xffd5dade));
    chrome.addColour (0.55, Colour (0xffa4acb3));
    g.setGradientFill (chrome);
    g.fillPath (mainPath);

    GlyphArrangement second;
    second.addLineOfText (Font (FontOptions (19.0f, Font::plain)), "bass", mainBounds.getRight() + 6.0f, baseline);
    Path secondPath;
    second.createPath (secondPath);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillPath (secondPath, AffineTransform::translation (1.0f, 1.0f));
    g.setColour (VirusColours::blue);
    g.fillPath (secondPath);

    Path swoosh;
    const float sx = mainBounds.getX() + 2.0f, sy = baseline + 3.5f;
    const float ex = secondPath.getBounds().getRight();
    swoosh.startNewSubPath (sx, sy);
    swoosh.quadraticTo ((sx + ex) * 0.5f, sy + 3.5f, ex + 6.0f, sy - 2.0f);
    g.setGradientFill (ColourGradient (VirusColours::blue.withAlpha (0.0f), sx, sy, VirusColours::blue, ex, sy, false));
    g.strokePath (swoosh, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void ReesePanel::paint (Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // --- top strip ----------------------------------------------------------------
    auto top = Rectangle<float> (0.0f, 0.0f, (float) kWidth, (float) kTopStripH);
    g.setGradientFill (ColourGradient (Colour (0xff4a4e53), 0.0f, 0.0f, Colour (0xff2b2e32), 0.0f, top.getBottom(), false));
    g.fillRect (top);
    g.setColour (Colours::white.withAlpha (0.18f));
    g.drawHorizontalLine (0, 0.0f, (float) kWidth);
    g.setColour (VirusColours::bevelDark);
    g.drawHorizontalLine (kTopStripH - 1, 0.0f, (float) kWidth);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (kTopStripH, 0.0f, (float) kWidth);

    drawWordmark (g, { 14.0f, 0.0f, 260.0f, (float) kTopStripH });
    CustomLookAndFeel::drawSmallLabel (g, "Preset", { 600.0f, 8.0f, 54.0f, 19.0f }, Justification::centredRight);

    // --- panels ---------------------------------------------------------------------
    auto divider = [&g] (float y, float x0, float x1)
    {
        g.setColour (VirusColours::bevelDark);
        g.drawHorizontalLine ((int) y, x0, x1);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) y + 1, x0, x1);
    };

    for (int o = 0; o < kNumOscs; ++o)
    {
        const auto& c = osc[o];
        const float x = c.panel.getX();
        CustomLookAndFeel::drawPanel (g, c.panel, "Osc " + String (o + 1));
        CustomLookAndFeel::drawSmallLabel (g, "Wave", { x + 12.0f, 64.0f, 124.0f, 12.0f }, Justification::centredLeft);
        CustomLookAndFeel::drawSmallLabel (g, "Octave", { x + 144.0f, 64.0f, 128.0f, 12.0f }, Justification::centredLeft);
        divider (108.0f, x + 12.0f, x + 268.0f);
        divider (196.0f, x + 12.0f, x + 268.0f);
    }

    CustomLookAndFeel::drawPanel (g, filterPanel, "Filter");
    {
        // cutoff readout in the header bar, right-aligned
        auto header = filterPanel.withHeight (18.0f).reduced (10.0f, 0.0f);
        g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
        g.setColour (Colours::black.withAlpha (0.45f));
        g.drawText (cutoffText, header.translated (0.0f, 1.0f), Justification::centredRight);
        g.setColour (Colour (0xffc9e6ff));
        g.drawText (cutoffText, header, Justification::centredRight);
    }
    CustomLookAndFeel::drawSmallLabel (g, "Type", { 592.0f, 64.0f, 130.0f, 12.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawSmallLabel (g, "Slope", { 730.0f, 64.0f, 130.0f, 12.0f }, Justification::centredLeft);
    {
        // "FILTER ENVELOPE" between two divider lines
        const float y = 208.0f;
        g.setFont (CustomLookAndFeel::uiFont (9.5f, true));
        const float tw = GlyphArrangement::getStringWidth (g.getCurrentFont(), "FILTER ENVELOPE") + 12.0f;
        const float cx = filterPanel.getCentreX();
        divider (y, 592.0f, cx - tw * 0.5f);
        divider (y, cx + tw * 0.5f, 812.0f);
        g.setColour (VirusColours::labelText);
        g.drawText ("FILTER ENVELOPE", Rectangle<float> (cx - tw * 0.5f, y - 6.0f, tw, 12.0f), Justification::centred);
    }

    CustomLookAndFeel::drawPanel (g, voicePanel,  "Voice");
    CustomLookAndFeel::drawSmallLabel (g, "Mode", { 16.0f, 332.0f, 68.0f, 12.0f }, Justification::centredLeft);
    CustomLookAndFeel::drawPanel (g, lowPanel,    "Low End");
    CustomLookAndFeel::drawPanel (g, stereoPanel, "Stereo / Output");
    CustomLookAndFeel::drawPanel (g, satPanel,    "Saturation");
    CustomLookAndFeel::drawPanel (g, ampPanel,    "Amp Envelope");

    // --- bottom bar -----------------------------------------------------------------
    auto bar = Rectangle<float> (0.0f, (float) kBottomBarY, (float) kWidth, (float) (kHeight - kBottomBarY));
    g.setGradientFill (ColourGradient (Colour (0xff17191b), 0.0f, bar.getY(), Colour (0xff0c0d0e), 0.0f, bar.getBottom(), false));
    g.fillRect (bar);
    g.setColour (Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine (kBottomBarY, 0.0f, (float) kWidth);

    auto tab = Rectangle<float> (8.0f, bar.getY() + 7.0f, 52.0f, 18.0f);
    g.setGradientFill (ColourGradient (VirusColours::headerTop, 0.0f, tab.getY(), VirusColours::headerBottom, 0.0f, tab.getBottom(), false));
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (VirusColours::bevelDark);
    g.drawRoundedRectangle (tab, 3.0f, 1.0f);
    g.setColour (VirusColours::headerText);
    g.setFont (CustomLookAndFeel::uiFont (10.5f, true));
    g.drawText ("BASS", tab, Justification::centred);

    auto inset = bar.reduced (70.0f, 7.0f);
    CustomLookAndFeel::drawInsetBox (g, inset, 3.0f);
    g.setFont (CustomLookAndFeel::uiFont (13.0f, true));
    g.setColour (Colour (0xffc8cdd2));
    g.drawText ("REESE", inset, Justification::centred);
}

//==============================================================================
void ReesePanel::timerCallback()
{
    auto& gui = processor.gui;

    for (int o = 0; o < kNumOscs; ++o)
        osc[o].octLcd.setContent ({}, formatOctave (roundToInt (raw (oscParam (o, ParamIDs::octave)))) + " st");

    const auto newCutoffText = formatCutoff (raw (ParamIDs::cutoff));
    if (newCutoffText != cutoffText)
    {
        cutoffText = newCutoffText;
        repaint (filterPanel.withHeight (19.0f).toNearestInt());
    }
    updateActiveStates();

    scope.update (gui.scopeL, gui.scopeR, ReeseAudioProcessor::GuiState::kScopeSize,
                  gui.scopeWrite.load (std::memory_order_acquire));

    const int voices = gui.activeVoices.load();
    const float monoHz = raw (ParamIDs::monoBass), lockHz = raw (ParamIDs::phaseLock);
    scope.setInfo ({ getPlayModeNames()[(int) raw (ParamIDs::playMode)].toUpperCase()
                         + "   " + String (voices) + (voices == 1 ? " NOTE" : " NOTES"),
                     "MONO BASS     " + (monoHz >= 1.0f ? "below " + formatLowHz (monoHz) : String ("Off")),
                     "PHASE LOCK    " + (lockHz >= 1.0f ? "below " + formatLowHz (lockHz) : String ("Off")),
                     "SPREAD        " + formatSpread (raw (ParamIDs::spread)) });

    const int program = processor.getCurrentProgram();
    if (program != lastShownProgram)
    {
        lastShownProgram = program;
        presetBox.setSelectedId (program + 1, dontSendNotification);
    }
}

//==============================================================================
// Resizable editor: the panel is laid out at its base size and scaled as a whole.
//==============================================================================
ReeseAudioProcessorEditor::ReeseAudioProcessorEditor (ReeseAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p), panel (p)
{
    // Read the remembered size first: setting up the resize limits below briefly
    // resizes the window, and that must not overwrite the saved size.
    const int savedWidth = processor.gui.editorWidth.load();

    addAndMakeVisible (panel);

    setResizable (true, true);
    setResizeLimits (kWidth * 7 / 10, kHeight * 7 / 10, kWidth * 2, kHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);

    const int width = savedWidth > 0 ? jlimit (kWidth * 7 / 10, kWidth * 2, savedWidth) : kWidth;
    setSize (width, roundToInt (width * (double) kHeight / (double) kWidth));
    sizeRestored = true;
}

void ReeseAudioProcessorEditor::resized()
{
    panel.setBounds (0, 0, kWidth, kHeight);
    panel.setTransform (AffineTransform::scale ((float) getWidth() / (float) kWidth));

    if (sizeRestored)
        processor.gui.editorWidth.store (getWidth());
}
