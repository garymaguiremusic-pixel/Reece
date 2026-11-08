#pragma once

#include <juce_dsp/juce_dsp.h>

namespace rs
{
    struct SaturationSettings
    {
        bool on = false;
        int type = 0;              // Soft, Tape, Tube, Hard Clip, Foldback, Fuzz
        float driveDb = 6.0f;
        float tone = 1.0f;         // 0 = dark .. 1 = open
        float mix = 1.0f;          // 0..1
        float levelDb = 0.0f;      // wet level
    };

    /** The saturation curve on its own (also drawn by the editor). Same curves as KickDrum. */
    float saturate (int type, float x) noexcept;

    //==============================================================================
    /**
        The KickDrum saturation: 4x oversampled with linear-phase FIR half-band filters,
        a 5 Hz DC blocker on the asymmetric curves, a one-pole Tone control, Mix and Level.

        The latency is constant (the dry path is delayed to match) and reported to the
        host, so the timing is the same whether Saturation is on or off.
    */
    class Saturation
    {
    public:
        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset();
        int getLatencySamples() const noexcept   { return latency; }

        void process (juce::AudioBuffer<float>&, const SaturationSettings&) noexcept;

    private:
        void processWet (juce::AudioBuffer<float>&, const SaturationSettings&) noexcept;

        double sampleRate = 44100.0;
        int channels = 2, latency = 0;
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

        juce::AudioBuffer<float> dryDelay;      // latency compensation for the dry path
        int delayWrite = 0;
        juce::AudioBuffer<float> dryCopy;

        float toneState[2] {}, dcX[2] {}, dcY[2] {};
        juce::SmoothedValue<float> driveSmooth, mixSmooth, levelSmooth;
    };
}
