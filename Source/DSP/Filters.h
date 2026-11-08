#pragma once

#include <cmath>
#include <algorithm>

namespace rs
{
    /** Smooth, cheap tanh-like soft clipper (exact enough for |x| < 3, clamps beyond). */
    inline float softClip (float x) noexcept
    {
        x = std::clamp (x, -3.0f, 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    inline float svfG (float hz, float sampleRate) noexcept
    {
        const float f = std::clamp (hz, 5.0f, sampleRate * 0.49f);
        return std::tan (3.14159265f * f / sampleRate);
    }

    //==============================================================================
    /** Linear trapezoidal (zero-delay feedback) state variable filter, used for the crossovers. */
    struct LinearSvf
    {
        float ic1 = 0.0f, ic2 = 0.0f;

        void reset() noexcept   { ic1 = ic2 = 0.0f; }

        /** Returns low-pass, writes high-pass. k = 1/Q. */
        inline float process (float v0, float g, float k, float& hp) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float v3 = v0 - ic2;
            const float v1 = a1 * ic1 + g * a1 * v3;
            const float v2 = ic2 + g * v1;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            hp = v0 - k * v1 - v2;
            return v2;
        }
    };

    /**
        Linkwitz-Riley 24 dB/oct crossover (two Butterworth sections per band).
        low + high is all-pass, so splitting and recombining keeps a flat response.
        The two bands may be fed from different sources (see the phase lock).
    */
    struct LR4Crossover
    {
        LinearSvf lp1, lp2, hp1, hp2;

        void reset() noexcept   { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }

        inline float lowPass (float x, float g) noexcept
        {
            float h;
            const float a = lp1.process (x, g, 1.41421356f, h);
            return lp2.process (a, g, 1.41421356f, h);
        }

        inline float highPass (float x, float g) noexcept
        {
            float h1, h2;
            hp1.process (x, g, 1.41421356f, h1);
            hp2.process (h1, g, 1.41421356f, h2);
            return h2;
        }
    };

    //==============================================================================
    /**
        Analog-style multimode filter: a cascade of 1-3 zero-delay-feedback state
        variable stages (12 / 24 / 36 dB/oct) with Butterworth spacing. Resonance
        raises the Q of the last stage. Each stage has soft saturation on its input
        and on its band-pass integrator, like an OTA filter: the resonance compresses
        and stays round instead of whistling or blowing up, and the cutoff can be swept
        per sample without zipper noise or instability.
    */
    class AnalogFilter
    {
    public:
        enum Type { LowPass = 0, BandPass, HighPass };

        void reset() noexcept
        {
            for (auto& s : stages)
                s.ic1 = s.ic2 = 0.0f;
        }

        /** type = Type, numStages = 1..3, resonance = 0..1. Call before process when they change. */
        void setMode (int newType, int newStages, float resonance) noexcept
        {
            type = newType;
            numStages = std::clamp (newStages, 1, 3);
            static constexpr float butterQ[3][3] = { { 0.70710678f, 0.0f, 0.0f },
                                                     { 0.54119610f, 1.30656296f, 0.0f },
                                                     { 0.51763809f, 0.70710678f, 1.93185165f } };
            const float r = std::clamp (resonance, 0.0f, 1.0f);
            for (int i = 0; i < numStages; ++i)
            {
                float q = type == BandPass ? 0.70710678f : butterQ[numStages - 1][i];
                if (i == numStages - 1)
                    q += r * r * (22.0f - q);
                k[i] = 1.0f / q;
                // band-pass: unity peak per stage, the resonant stage peaks at sqrt (Q)
                bpGain[i] = i == numStages - 1 ? std::sqrt (k[i]) : k[i];
            }
        }

        /** g = svfG (cutoff, sampleRate). */
        inline float process (float x, float g) noexcept
        {
            for (int i = 0; i < numStages; ++i)
            {
                auto& s = stages[i];
                const float kk = k[i];

                // input stage: gentle OTA-style saturation
                const float v0 = 2.0f * softClip (x * 0.5f);

                const float a1 = 1.0f / (1.0f + g * (g + kk));
                const float v3 = v0 - s.ic2;
                const float v1 = a1 * s.ic1 + g * a1 * v3;
                const float v2 = s.ic2 + g * v1;
                s.ic1 = 2.0f * v1 - s.ic1;
                s.ic2 = 2.0f * v2 - s.ic2;

                // saturating integrator: limits the resonant peak smoothly
                s.ic1 = 1.6f * softClip (s.ic1 * 0.625f);

                switch (type)
                {
                    case HighPass:  x = v0 - kk * v1 - v2; break;
                    case BandPass:  x = v1 * bpGain[i]; break;
                    default:        x = v2; break;
                }
            }
            return x;
        }

    private:
        struct Stage { float ic1 = 0.0f, ic2 = 0.0f; };
        Stage stages[3];
        float k[3] { 1.41421356f, 1.41421356f, 1.41421356f };
        float bpGain[3] { 1.0f, 1.0f, 1.0f };
        int type = LowPass, numStages = 2;
    };

    //==============================================================================
    /** ADSR with an analog-style curve: linear-ish attack, exponential decay and release. */
    class Envelope
    {
    public:
        enum class Stage { Idle, Attack, Decay, Sustain, Release };

        void setSampleRate (double sr) noexcept   { sampleRate = (float) sr; }

        void setParameters (float attackMs, float decayMs, float sustain, float releaseMs) noexcept
        {
            // attack aims past 1.0, so the curve is mostly linear but still analog-shaped
            attackCoef = coefFor (attackMs, 1.61f);   // 0 -> 1 while aiming at 1.25
            decayCoef = coefFor (decayMs, 4.6f);
            releaseCoef = coefFor (releaseMs, 4.6f);
            sustainLevel = std::clamp (sustain, 0.0f, 1.0f);
        }

        /** Starts the attack from the current level (no click when retriggered while sounding). */
        void noteOn() noexcept    { stage = Stage::Attack; }
        void noteOff() noexcept   { if (stage != Stage::Idle) stage = Stage::Release; }
        void reset() noexcept     { stage = Stage::Idle; level = 0.0f; }

        bool isActive() const noexcept   { return stage != Stage::Idle; }
        bool isReleasing() const noexcept { return stage == Stage::Release; }
        float getLevel() const noexcept  { return level; }

        inline float next() noexcept
        {
            switch (stage)
            {
                case Stage::Attack:
                    level += (1.25f - level) * attackCoef;
                    if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; }
                    break;
                case Stage::Decay:
                    level += (sustainLevel - level) * decayCoef;
                    if (std::abs (level - sustainLevel) < 1.0e-4f) { level = sustainLevel; stage = Stage::Sustain; }
                    break;
                case Stage::Sustain:
                    level += (sustainLevel - level) * 0.001f;    // follows sustain changes smoothly
                    break;
                case Stage::Release:
                    level -= level * releaseCoef;
                    if (level < 1.0e-4f) { level = 0.0f; stage = Stage::Idle; }
                    break;
                case Stage::Idle:
                default:
                    break;
            }
            return level;
        }

    private:
        float coefFor (float ms, float curve) const noexcept
        {
            const float samples = std::max (1.0f, ms * 0.001f * sampleRate);
            // reach ~the target in 'ms': curve = ln(1/remaining)
            return 1.0f - std::exp (-curve / samples);
        }

        float sampleRate = 44100.0f;
        float attackCoef = 0.5f, decayCoef = 0.01f, releaseCoef = 0.01f, sustainLevel = 1.0f;
        float level = 0.0f;
        Stage stage = Stage::Idle;
    };
}
