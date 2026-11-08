#pragma once

#include <cstdint>
#include "Oscillator.h"
#include "Filters.h"

namespace rs
{
    struct OscSettings
    {
        int wave = WaveSaw;
        float pitchSemis = 0.0f;     // octave * 12 + semi
        float fineCents = 0.0f;
        int voices = 1;
        float detuneCents = 0.0f;    // unison spread (outer voices sit at +- this)
        float pw = 0.5f;             // 0..1
        float phase = 0.0f;          // start phase 0..1
        bool retrig = false;         // PHASE button: restart at 'phase' on every note (off = free running)
        bool fmOn = false;           // FM button: the other oscillator modulates this one
        float fmAmount = 0.0f;       // 0..1
        int noiseFilter = 0;         // Noise wave only: 0 = LP, 1 = BP, 2 = HP
        float noiseCutoff = 100.0f;  // knob 0..100 (20 Hz .. 20 kHz)
        float noiseReso = 0.0f;      // 0..1
        float pan = 0.0f;            // -1..1
        float gain = 0.8f;           // 0..1
    };

    struct EngineSettings
    {
        OscSettings osc[kNumOscs];
        int playMode = ModeMono;
        float glideMs = 0.0f;
        float phaseLockHz = 0.0f;    // 0 = off

        int filterType = FilterLP;
        int filterStages = 2;        // 1..3 = 12 / 24 / 36 dB/oct
        float cutoff = 62.0f;        // knob value 0..100
        float resonance = 0.0f;      // 0..1
        float envSemis = 0.0f;       // filter envelope depth
        float keyTrack = 0.5f;       // 0..1

        float fA = 1.0f, fD = 300.0f, fS = 0.3f, fR = 200.0f;   // ms / level
        float aA = 1.0f, aD = 300.0f, aS = 1.0f, aR = 100.0f;
    };

    /** Values shared by all voices for one chunk (computed once by the engine). */
    struct ChunkShared
    {
        static constexpr int kMax = 256;
        float cutoffSemis[kMax];     // smoothed cutoff, in semitones above 20 Hz
        float lockG[kMax];           // crossover coefficient for the phase lock (0 = off)
        float resonance = 0.0f;
        float bendSemis = 0.0f;

        // per oscillator unison tables
        int numVoices[kNumOscs] {};
        float ratio[kNumOscs][kMaxUnison] {};
        float gainL[kNumOscs][kMaxUnison] {}, gainR[kNumOscs][kMaxUnison] {};
        float noiseL[kNumOscs] {}, noiseR[kNumOscs] {};
        float noiseG[kNumOscs] {}, noiseK[kNumOscs] {};   // noise filter coefficients
        float fmDepth[kNumOscs] {};                        // 0 = FM off
        float lockFineCents = 0.0f;
    };

    //==============================================================================
    class Voice
    {
    public:
        void prepare (double sampleRate, std::uint32_t seed);
        void reset();

        /** Starts a note. glideFromCurrent = slide from the pitch the voice is playing now. */
        void start (int note, float velocity, bool glideFromCurrent, const EngineSettings&);
        /** Changes the pitch without retriggering (legato). */
        void legatoTo (int note, const EngineSettings&);
        void release();
        void kill()                                   { reset(); }

        bool isActive() const noexcept                { return ampEnv.isActive() || pending.waiting; }
        bool isHeld() const noexcept                  { return held; }
        bool isReleasing() const noexcept             { return ampEnv.isReleasing() && ! pending.waiting; }
        int getNote() const noexcept                  { return pending.waiting ? pending.note : note; }
        std::uint64_t getAge() const noexcept         { return age; }
        void setAge (std::uint64_t a) noexcept        { age = a; }
        float getLevel() const noexcept               { return ampEnv.getLevel(); }

        /** Adds this voice's output to left / right. */
        void render (float* left, float* right, int numSamples, const EngineSettings&, const ChunkShared&) noexcept;

    private:
        void doStart (int note, float velocity, bool glideFromCurrent, const EngineSettings&);
        bool willResetPhases (const EngineSettings&) const noexcept;

        double sampleRate = 44100.0;
        float invSampleRate = 1.0f / 44100.0f;

        int note = 60;
        float velocity = 1.0f;
        float pitch = 60.0f, targetPitch = 60.0f;
        bool held = false;
        std::uint64_t age = 0;

        float phases[kNumOscs][kMaxUnison] {};
        float lockPhase[kNumOscs] {};
        Noise noise[kNumOscs], random;
        LinearSvf noiseFilter[kNumOscs];
        float fmPrev[kNumOscs] {}, fmLockPrev[kNumOscs] {};   // last output of each oscillator (FM modulators)
        float drift = 0.0f;          // tiny per-voice cutoff offset, like analog component tolerance

        Envelope ampEnv, fltEnv;
        AnalogFilter filterL, filterR;
        LR4Crossover xoverL, xoverR, xoverLock;

        // de-click: fade out quickly before restarting the phases of a sounding voice
        struct Pending { bool waiting = false, releaseAfter = false, glide = false; int note = 60; float velocity = 1.0f; };
        Pending pending;
        float fadeGain = 1.0f, fadeStep = 0.0f;
    };

    //==============================================================================
    class SynthEngine
    {
    public:
        void prepare (double sampleRate);
        void reset();

        /** Call once per block before render. */
        void setSettings (const EngineSettings&);

        void noteOn (int note, float velocity);
        void noteOff (int note);
        void allNotesOff();
        void pitchWheel (int value14bit);

        /** Renders (adds) into left/right. Splits into chunks internally. */
        void render (float* left, float* right, int numSamples) noexcept;

        int getNumActiveVoices() const noexcept;

    private:
        void renderChunk (float* left, float* right, int numSamples) noexcept;
        void prepareShared() noexcept;
        Voice* findVoiceToUse() noexcept;

        double sampleRate = 44100.0;
        EngineSettings settings;
        Voice voices[kMaxPoly];
        ChunkShared shared;
        std::uint64_t ageCounter = 0;
        int lastPlayMode = -1;

        int noteStack[32] {};
        int stackSize = 0;
        float bendSemis = 0.0f;

        // smoothed controls (one-pole, per sample)
        float cutoffSm = 62.0f, resSm = 0.0f, lockSm = 0.0f, smoothCoef = 0.001f;
        bool firstBlock = true;
    };
}
