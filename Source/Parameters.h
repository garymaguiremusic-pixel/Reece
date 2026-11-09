#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

//==============================================================================
// Parameter IDs, choice lists and the knob -> real-unit mappings shared by the
// DSP, the processor and the editor.
//
// Knobs move in whole steps (Shift = 0.1 steps), so each parameter's range is
// chosen so that one step is a musically useful amount (1 cent, 1 Hz, 1 % ...).
// Cutoff and Resonance are the exception: they move continuously.
//==============================================================================
namespace rs
{
    static constexpr int kNumOscs = 2;
    static constexpr int kMaxUnison = 8;
    static constexpr int kMaxPoly = 8;

    //==============================================================================
    namespace ParamIDs
    {
        // Per oscillator, see oscParam()
        inline constexpr const char* wave   = "Wave";
        inline constexpr const char* octave = "Octave";
        inline constexpr const char* semi   = "Semi";
        inline constexpr const char* fine   = "Fine";
        inline constexpr const char* voices = "Voices";
        inline constexpr const char* detune = "Detune";
        inline constexpr const char* pw     = "PW";
        inline constexpr const char* phase  = "Phase";
        inline constexpr const char* retrig = "Retrig";   // the oscillator's PHASE button
        inline constexpr const char* fmOn     = "FmOn";       // FM: the other oscillator modulates this one
        inline constexpr const char* fmAmount = "FmAmount";
        inline constexpr const char* noiseFilter = "NoiseFilter";   // Noise wave only
        inline constexpr const char* noiseCutoff = "NoiseCutoff";
        inline constexpr const char* noiseReso   = "NoiseReso";
        inline constexpr const char* pan    = "Pan";
        inline constexpr const char* level  = "Level";

        // Voice
        inline constexpr const char* playMode = "playMode";
        inline constexpr const char* glide    = "glide";

        // Low end
        inline constexpr const char* monoBass  = "monoBass";
        inline constexpr const char* phaseLock = "phaseLock";

        // Filter
        inline constexpr const char* fltType   = "fltType";
        inline constexpr const char* fltSlope  = "fltSlope";
        inline constexpr const char* cutoff    = "cutoff";
        inline constexpr const char* resonance = "resonance";
        inline constexpr const char* fltEnv    = "fltEnv";
        inline constexpr const char* keyTrack  = "keyTrack";
        inline constexpr const char* fAttack   = "fAttack";
        inline constexpr const char* fDecay    = "fDecay";
        inline constexpr const char* fSustain  = "fSustain";
        inline constexpr const char* fRelease  = "fRelease";

        // Envelope tempo sync (Attack / Decay / Release as note values)
        inline constexpr const char* fSync        = "fSync";
        inline constexpr const char* fAttackSync  = "fAttackSync";
        inline constexpr const char* fDecaySync   = "fDecaySync";
        inline constexpr const char* fReleaseSync = "fReleaseSync";
        inline constexpr const char* aSync        = "aSync";
        inline constexpr const char* aAttackSync  = "aAttackSync";
        inline constexpr const char* aDecaySync   = "aDecaySync";
        inline constexpr const char* aReleaseSync = "aReleaseSync";

        // Amp
        inline constexpr const char* aAttack  = "aAttack";
        inline constexpr const char* aDecay   = "aDecay";
        inline constexpr const char* aSustain = "aSustain";
        inline constexpr const char* aRelease = "aRelease";

        // Saturation (same design as KickDrum)
        inline constexpr const char* satOn    = "satOn";
        inline constexpr const char* satType  = "satType";
        inline constexpr const char* satDrive = "satDrive";
        inline constexpr const char* satTone  = "satTone";
        inline constexpr const char* satMix   = "satMix";
        inline constexpr const char* satLevel = "satLevel";

        // LFO -> filter cutoff
        inline constexpr const char* lfoOn     = "lfoOn";
        inline constexpr const char* lfoShape  = "lfoShape";
        inline constexpr const char* lfoSync   = "lfoSync";
        inline constexpr const char* lfoRate   = "lfoRate";       // free rate, knob 0..100
        inline constexpr const char* lfoDiv    = "lfoDiv";        // synced note value
        inline constexpr const char* lfoDepth  = "lfoDepth";      // semitones
        inline constexpr const char* lfoRetrig = "lfoRetrig";

        // Randomise targets
        inline constexpr const char* randLfo = "randLfo";
        inline constexpr const char* randSat = "randSat";

        // Output
        inline constexpr const char* spread = "spread";
        inline constexpr const char* volume = "volume";
    }

    /** Per-oscillator IDs, e.g. oscParam (1, "Fine") -> "osc2Fine". */
    inline juce::String oscParam (int osc, const char* name)   { return "osc" + juce::String (osc + 1) + name; }

    //==============================================================================
    enum Wave : int { WaveSaw = 0, WaveRamp, WaveSquare, WavePulse, WaveTriangle, WaveSine, WaveSawSquare, WaveNoise, NumWaves };
    enum PlayMode : int { ModeMono = 0, ModeLegato, ModePoly };
    enum FilterType : int { FilterLP = 0, FilterBP, FilterHP };

    inline juce::StringArray getWaveNames()      { return { "Saw", "Ramp", "Square", "Pulse", "Triangle", "Sine", "Saw + Square", "Noise" }; }
    inline juce::StringArray getPlayModeNames()  { return { "Mono", "Legato", "Poly" }; }
    inline juce::StringArray getFilterTypeNames(){ return { "Low Pass", "Band Pass", "High Pass" }; }
    inline juce::StringArray getNoiseFilterNames() { return { "Low Pass", "Band Pass", "High Pass" }; }
    inline juce::StringArray getSlopeNames()     { return { "12 dB/oct", "24 dB/oct", "36 dB/oct" }; }
    inline juce::StringArray getSatTypeNames()   { return { "Soft", "Tape", "Tube", "Hard Clip", "Foldback", "Fuzz" }; }

    enum LfoShape : int { LfoSine = 0, LfoTriangle, LfoSawUp, LfoSawDown, LfoSquare, LfoSampleHold, NumLfoShapes };
    inline juce::StringArray getLfoShapeNames() { return { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold" }; }

    /** LFO sync note values, slowest first, and their length in beats. */
    inline juce::StringArray getLfoDivisionNames()
    {
        return { "8/1", "4/1", "2/1", "1/1", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/64" };
    }
    inline double lfoDivisionBeats (int index)
    {
        static constexpr double beats[] = { 32.0, 16.0, 8.0, 4.0, 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
    }
    inline float lfoRateToHz (float v)   { return 0.05f * std::pow (400.0f, juce::jlimit (0.0f, 100.0f, v) * 0.01f); }   // 0.05 .. 20 Hz

    /** Envelope sync divisions, shortest first: 1/512 .. 1/1 (1/1 = one bar of 4/4). */
    inline juce::StringArray getSyncDivisionNames()
    {
        return { "1/512", "1/256", "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
    }
    static constexpr int kNumSyncDivisions = 10;
    static constexpr double kDefaultBpm = 120.0;

    inline float syncDivisionToMs (int index, double bpm)
    {
        const int denominator = 512 >> juce::jlimit (0, kNumSyncDivisions - 1, index);
        const double beats = 4.0 / (double) denominator;
        return (float) (beats * 60000.0 / juce::jmax (20.0, bpm));
    }

    //==============================================================================
    // Knob -> unit mappings
    inline float cutoffToHz (float v)        { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 100.0f, v) * 0.01f); }   // 20 Hz .. 20 kHz
    inline float envTimeToMs (float v)       { return 0.5f * std::pow (20000.0f, juce::jlimit (0.0f, 100.0f, v) * 0.01f); }   // 0.5 ms .. 10 s
    inline float glideToMs (float v)         { return v <= 0.0f ? 0.0f : std::pow (2000.0f, juce::jlimit (0.0f, 100.0f, v) * 0.01f); } // off, 1 ms .. 2 s
    inline float fltEnvToSemis (float v)     { return v; }   // the knob is in semitones: +-96 (8 octaves), 1 per step

    //==============================================================================
    // Value text
    inline juce::String trimmed (float v, int decimals = 1)
    {
        const float r = std::round (v * 10.0f) / 10.0f;
        return std::abs (r - std::round (r)) < 0.05f ? juce::String (juce::roundToInt (r)) : juce::String (r, decimals);
    }

    inline juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
            return juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
        return (hz < 100.0f ? juce::String (hz, 1) : juce::String (juce::roundToInt (hz))) + " Hz";
    }

    inline juce::String formatMs (float ms)
    {
        if (ms >= 1000.0f) return juce::String (ms / 1000.0f, 2) + " s";
        return (ms < 10.0f ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms))) + " ms";
    }

    inline juce::String formatSigned (float v, const juce::String& unit)
    {
        const auto t = trimmed (v);
        return (v > 0.04f ? "+" : "") + t + unit;
    }

    inline juce::String formatPercent (float v)  { return trimmed (v) + " %"; }
    inline juce::String formatDb (float v)       { return formatSigned (v, " dB"); }
    inline juce::String formatCents (float v)    { return formatSigned (v, " ct"); }
    inline juce::String formatSemis (float v)    { return formatSigned (v, " st"); }
    inline juce::String formatDegrees (float v)  { return trimmed (v) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); }
    inline juce::String formatEnvTime (float v)  { return formatMs (envTimeToMs (v)); }
    inline juce::String formatCutoff (float v)   { return formatHz (cutoffToHz (v)); }
    inline juce::String formatGlide (float v)    { return v <= 0.0f ? juce::String ("Off") : formatMs (glideToMs (v)); }
    inline juce::String formatLowHz (float v)    { return v < 1.0f ? juce::String ("Off") : trimmed (v) + " Hz"; }
    inline juce::String formatVoices (float v)   { const int n = juce::roundToInt (v); return juce::String (n) + (n == 1 ? " voice" : " voices"); }
    inline juce::String formatVolume (float v)   { return v <= -47.9f ? juce::String ("-inf") : formatDb (v); }
    inline juce::String formatOctave (int o)     { return o == 0 ? juce::String ("0") : (o > 0 ? "+" : "") + juce::String (o * 12); }

    inline juce::String formatPan (float v)
    {
        if (std::abs (v) < 0.05f) return "C";
        return (v < 0.0f ? "L " : "R ") + trimmed (std::abs (v));
    }

    inline juce::String formatSpread (float v)
    {
        if (v < 0.05f) return "Mono";
        return trimmed (v) + " %";
    }

    //==============================================================================
    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;
        constexpr int version = 1;

        auto addFloat = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                             std::function<String (float)> text, const String& unit = {})
        {
            layout.add (std::make_unique<AudioParameterFloat> (
                ParameterID { id, version }, name, range, def,
                AudioParameterFloatAttributes().withLabel (unit)
                    .withStringFromValueFunction ([text] (float v, int) { return text (v); })));
        };
        auto addInt = [&] (const String& id, const String& name, int lo, int hi, int def,
                           std::function<String (float)> text)
        {
            layout.add (std::make_unique<AudioParameterInt> (
                ParameterID { id, version }, name, lo, hi, def,
                AudioParameterIntAttributes().withStringFromValueFunction ([text] (int v, int) { return text ((float) v); })));
        };
        auto addChoice = [&] (const String& id, const String& name, const StringArray& items, int def)
        {
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, items, def));
        };
        auto addBool = [&] (const String& id, const String& name, bool def)
        {
            layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, version }, name, def));
        };

        // Oscillators -----------------------------------------------------------------
        for (int o = 0; o < kNumOscs; ++o)
        {
            const String n = "Osc " + String (o + 1) + " ";
            addChoice (oscParam (o, ParamIDs::wave), n + "Wave", getWaveNames(), WaveSaw);
            addInt    (oscParam (o, ParamIDs::octave), n + "Octave", -3, 3, 0, [] (float v) { return formatOctave ((int) v) + " st"; });
            addFloat  (oscParam (o, ParamIDs::semi), n + "Semi", { -12.0f, 12.0f }, 0.0f, formatSemis, "st");
            addFloat  (oscParam (o, ParamIDs::fine), n + "Fine", { -100.0f, 100.0f }, o == 0 ? -8.0f : 8.0f, formatCents, "ct");
            addInt    (oscParam (o, ParamIDs::voices), n + "Voices", 1, kMaxUnison, 2, formatVoices);
            addFloat  (oscParam (o, ParamIDs::detune), n + "Detune", { 0.0f, 100.0f }, 12.0f, [] (float v) { return trimmed (v) + " ct"; }, "ct");
            addFloat  (oscParam (o, ParamIDs::pw), n + "Pulse Width", { 1.0f, 99.0f }, 50.0f, formatPercent, "%");
            addFloat  (oscParam (o, ParamIDs::phase), n + "Phase", { 0.0f, 360.0f }, 0.0f, formatDegrees, "deg");
            addBool   (oscParam (o, ParamIDs::retrig), n + "Phase Retrigger", false);
            addBool   (oscParam (o, ParamIDs::fmOn), n + "FM", false);
            addFloat  (oscParam (o, ParamIDs::fmAmount), n + "FM Amount", { 0.0f, 100.0f }, 0.0f, formatPercent, "%");
            addChoice (oscParam (o, ParamIDs::noiseFilter), n + "Noise Filter", getNoiseFilterNames(), 0);
            addFloat  (oscParam (o, ParamIDs::noiseCutoff), n + "Noise Cutoff", { 0.0f, 100.0f }, 100.0f, formatCutoff);
            addFloat  (oscParam (o, ParamIDs::noiseReso), n + "Noise Resonance", { 0.0f, 100.0f }, 0.0f, formatPercent, "%");
            addFloat  (oscParam (o, ParamIDs::pan), n + "Pan", { -100.0f, 100.0f }, 0.0f, formatPan);
            addFloat  (oscParam (o, ParamIDs::level), n + "Level", { 0.0f, 100.0f }, 80.0f, formatPercent, "%");
        }

        // Voice ---------------------------------------------------------------------------
        addChoice (ParamIDs::playMode, "Play Mode", getPlayModeNames(), ModeMono);
        addFloat  (ParamIDs::glide, "Glide", { 0.0f, 100.0f }, 0.0f, formatGlide);

        // Low end -------------------------------------------------------------------------
        addFloat (ParamIDs::monoBass, "Mono Bass", { 0.0f, 200.0f }, 120.0f, formatLowHz, "Hz");
        addFloat (ParamIDs::phaseLock, "Phase Lock", { 0.0f, 200.0f }, 0.0f, formatLowHz, "Hz");

        // Filter --------------------------------------------------------------------------
        addChoice (ParamIDs::fltType, "Filter Type", getFilterTypeNames(), FilterLP);
        addChoice (ParamIDs::fltSlope, "Filter Slope", getSlopeNames(), 1);
        addFloat  (ParamIDs::cutoff, "Cutoff", { 0.0f, 100.0f }, 62.0f, formatCutoff);
        addFloat  (ParamIDs::resonance, "Resonance", { 0.0f, 100.0f }, 18.0f, formatPercent, "%");
        addFloat  (ParamIDs::fltEnv, "Filter Env Amount", { -96.0f, 96.0f }, 19.0f, formatSemis, "st");
        addFloat  (ParamIDs::keyTrack, "Key Track", { 0.0f, 100.0f }, 50.0f, formatPercent, "%");
        addFloat  (ParamIDs::fAttack, "Filter Attack", { 0.0f, 100.0f }, 0.0f, formatEnvTime);
        addFloat  (ParamIDs::fDecay, "Filter Decay", { 0.0f, 100.0f }, 55.0f, formatEnvTime);
        addFloat  (ParamIDs::fSustain, "Filter Sustain", { 0.0f, 100.0f }, 30.0f, formatPercent, "%");
        addFloat  (ParamIDs::fRelease, "Filter Release", { 0.0f, 100.0f }, 45.0f, formatEnvTime);

        // Envelope sync -------------------------------------------------------------------------
        addBool   (ParamIDs::fSync, "Filter Env Sync", false);
        addChoice (ParamIDs::fAttackSync,  "Filter Attack Sync",  getSyncDivisionNames(), 0);
        addChoice (ParamIDs::fDecaySync,   "Filter Decay Sync",   getSyncDivisionNames(), 6);
        addChoice (ParamIDs::fReleaseSync, "Filter Release Sync", getSyncDivisionNames(), 5);
        addBool   (ParamIDs::aSync, "Amp Env Sync", false);
        addChoice (ParamIDs::aAttackSync,  "Amp Attack Sync",  getSyncDivisionNames(), 0);
        addChoice (ParamIDs::aDecaySync,   "Amp Decay Sync",   getSyncDivisionNames(), 6);
        addChoice (ParamIDs::aReleaseSync, "Amp Release Sync", getSyncDivisionNames(), 4);

        // Amp -------------------------------------------------------------------------------
        addFloat (ParamIDs::aAttack, "Amp Attack", { 0.0f, 100.0f }, 10.0f, formatEnvTime);
        addFloat (ParamIDs::aDecay, "Amp Decay", { 0.0f, 100.0f }, 50.0f, formatEnvTime);
        addFloat (ParamIDs::aSustain, "Amp Sustain", { 0.0f, 100.0f }, 100.0f, formatPercent, "%");
        addFloat (ParamIDs::aRelease, "Amp Release", { 0.0f, 100.0f }, 35.0f, formatEnvTime);

        // Saturation --------------------------------------------------------------------------
        addBool   (ParamIDs::satOn, "Saturation On", false);
        addChoice (ParamIDs::satType, "Saturation Type", getSatTypeNames(), 1);
        addFloat  (ParamIDs::satDrive, "Drive", { 0.0f, 36.0f }, 6.0f, formatDb, "dB");
        addFloat  (ParamIDs::satTone, "Tone", { 0.0f, 100.0f }, 100.0f, formatPercent, "%");
        addFloat  (ParamIDs::satMix, "Saturation Mix", { 0.0f, 100.0f }, 100.0f, formatPercent, "%");
        addFloat  (ParamIDs::satLevel, "Saturation Level", { -24.0f, 6.0f }, 0.0f, formatDb, "dB");

        // LFO -----------------------------------------------------------------------------------
        addBool   (ParamIDs::lfoOn, "LFO On", false);
        addChoice (ParamIDs::lfoShape, "LFO Shape", getLfoShapeNames(), LfoSine);
        addBool   (ParamIDs::lfoSync, "LFO Sync", true);
        addFloat  (ParamIDs::lfoRate, "LFO Rate", { 0.0f, 100.0f }, 50.0f,
                   [] (float v) { const float hz = lfoRateToHz (v); return juce::String (hz, hz < 1.0f ? 2 : 1) + " Hz"; });
        addChoice (ParamIDs::lfoDiv, "LFO Sync Rate", getLfoDivisionNames(), 7);
        addFloat  (ParamIDs::lfoDepth, "LFO Depth", { 0.0f, 96.0f }, 24.0f, [] (float v) { return trimmed (v) + " st"; }, "st");
        addBool   (ParamIDs::lfoRetrig, "LFO Retrigger", false);

        // Randomise targets ------------------------------------------------------------------
        addBool (ParamIDs::randLfo, "Randomise LFO", true);
        addBool (ParamIDs::randSat, "Randomise Saturation", true);

        // Output --------------------------------------------------------------------------------
        addFloat (ParamIDs::spread, "Stereo Spread", { 0.0f, 200.0f }, 100.0f, formatSpread, "%");
        addFloat (ParamIDs::volume, "Volume", { -48.0f, 6.0f }, -3.0f, formatVolume, "dB");

        return layout;
    }
}
