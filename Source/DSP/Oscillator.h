#pragma once

#include <cmath>
#include <cstdint>
#include "../Parameters.h"

namespace rs
{
    /** 2-sample polynomial band-limited step residual (PolyBLEP). t = phase 0..1, dt = increment. */
    inline float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    /** Integrated PolyBLEP residual (PolyBLAMP), for slope changes such as the triangle's corners. */
    inline float polyBlamp (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t = t / dt - 1.0f;
            return -(1.0f / 3.0f) * t * t * t;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt + 1.0f;
            return (1.0f / 3.0f) * t * t * t;
        }
        return 0.0f;
    }

    inline float wrap01 (float p) noexcept   { return p - std::floor (p); }

    /** Small fast white noise generator (xorshift32), -1 .. 1. */
    struct Noise
    {
        std::uint32_t state = 0x9e3779b9u;
        void seed (std::uint32_t s) noexcept   { state = s != 0 ? s : 0x9e3779b9u; }
        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return (float) (int32_t) state * (1.0f / 2147483648.0f);
        }
    };

    /**
        One band-limited sample of the given wave at phase p (0..1) with increment dt.
        Saw / Ramp / Square / Pulse use PolyBLEP, Triangle uses PolyBLAMP, so the
        oscillators stay clean high up the keyboard and with big unison detunes.
        Noise is handled by the caller.
    */
    inline float renderWave (int wave, float p, float dt, float pw) noexcept
    {
        dt = std::min (dt, 0.5f);
        switch (wave)
        {
            case WaveSaw:
                return 2.0f * p - 1.0f - polyBlep (p, dt);

            case WaveRamp:
                return 1.0f - 2.0f * p + polyBlep (p, dt);

            case WaveSquare:
            {
                float y = p < 0.5f ? 1.0f : -1.0f;
                y += polyBlep (p, dt);
                y -= polyBlep (wrap01 (p + 0.5f), dt);
                return y;
            }

            case WavePulse:
            {
                // keep the edges at least a couple of samples apart, and remove the DC offset
                const float w = std::clamp (pw, dt * 2.0f, 1.0f - dt * 2.0f);
                float y = p < w ? 1.0f : -1.0f;
                y += polyBlep (p, dt);
                y -= polyBlep (wrap01 (p + 1.0f - w), dt);
                return y - (2.0f * w - 1.0f);
            }

            case WaveTriangle:
            {
                float y = 2.0f * std::abs (2.0f * p - 1.0f) - 1.0f;   // +1 at p = 0, -1 at p = 0.5
                y += 2.0f * dt * (polyBlamp (wrap01 (p + 0.5f), dt) - polyBlamp (p, dt));
                return y;
            }

            case WaveSine:
                return std::sin (6.283185307f * p);

            case WaveSawSquare:
            {
                const float saw = 2.0f * p - 1.0f - polyBlep (p, dt);
                float sq = p < 0.5f ? 1.0f : -1.0f;
                sq += polyBlep (p, dt);
                sq -= polyBlep (wrap01 (p + 0.5f), dt);
                return 0.6f * (saw + sq);
            }

            default:
                return 0.0f;
        }
    }
}
