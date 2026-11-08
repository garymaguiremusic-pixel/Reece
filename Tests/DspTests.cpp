#include <juce_core/juce_core.h>
#include "DSP/Oscillator.h"
#include "DSP/Filters.h"
#include "DSP/SynthEngine.h"
#include "PluginProcessor.h"
#include "Presets.h"

using namespace juce;
using namespace rs;

namespace
{
    constexpr double kRate = 48000.0;

    void setParam (ReeseAudioProcessor& p, const String& id, float value)
    {
        auto* param = p.apvts.getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    /** Renders 'seconds' of audio with one note held from the start (released at noteOffAt seconds, if > 0). */
    AudioBuffer<float> renderNote (ReeseAudioProcessor& p, int note, double seconds, double noteOffAt = -1.0)
    {
        constexpr int block = 256;
        const int total = (int) (seconds * kRate);
        AudioBuffer<float> out (2, total);
        AudioBuffer<float> buffer (2, block);
        const int offSample = noteOffAt > 0.0 ? (int) (noteOffAt * kRate) : -1;

        for (int pos = 0; pos < total; pos += block)
        {
            const int n = jmin (block, total - pos);
            buffer.setSize (2, n, false, false, true);
            MidiBuffer midi;
            if (pos == 0)
                midi.addEvent (MidiMessage::noteOn (1, note, 0.8f), 0);
            if (offSample >= pos && offSample < pos + n)
                midi.addEvent (MidiMessage::noteOff (1, note), offSample - pos);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, buffer, ch, 0, n);
        }
        return out;
    }

    bool allFinite (const AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }

    /** RMS of a 4th-order low-passed copy of (L + R) / 2 in consecutive windows. */
    std::vector<float> lowBandRms (const AudioBuffer<float>& b, float hz, int window)
    {
        LR4Crossover x;
        const float g = svfG (hz, (float) kRate);
        std::vector<float> result;
        double acc = 0.0;
        int count = 0;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float y = x.lowPass (0.5f * (b.getSample (0, i) + b.getSample (1, i)), g);
            acc += (double) y * y;
            if (++count == window)
            {
                result.push_back ((float) std::sqrt (acc / window));
                acc = 0.0;
                count = 0;
            }
        }
        return result;
    }

    std::unique_ptr<ReeseAudioProcessor> makeProcessor()
    {
        auto p = std::make_unique<ReeseAudioProcessor>();
        p->setRateAndBufferSizeDetails (kRate, 256);
        p->prepareToPlay (kRate, 256);
        return p;
    }
}

//==============================================================================
class OscillatorTests : public UnitTest
{
public:
    OscillatorTests() : UnitTest ("Oscillators", "DSP") {}

    void runTest() override
    {
        beginTest ("Every wave stays finite and bounded");
        for (int wave = 0; wave < WaveNoise; ++wave)
            for (float dt : { 0.0005f, 0.01f, 0.1f, 0.3f })
            {
                float peak = 0.0f, p = 0.123f;
                for (int i = 0; i < 20000; ++i)
                {
                    const float y = renderWave (wave, p, dt, 0.3f);
                    expect (std::isfinite (y));
                    peak = jmax (peak, std::abs (y));
                    p = wrap01 (p + dt);
                }
                expectLessThan (peak, 1.7f, "wave " + String (wave));
            }

        beginTest ("Band-limited saw / triangle are closer to the ideal than the naive ones");
        {
            // a fairly high note, where aliasing matters
            const double f = 2217.0, dt = f / kRate;
            auto ideal = [&] (int wave, double ph)
            {
                double y = 0.0;
                for (int k = 1; k * f < kRate * 0.5; ++k)
                {
                    if (wave == WaveSaw)
                        y += -2.0 / (MathConstants<double>::pi * k) * std::sin (MathConstants<double>::twoPi * k * ph);
                    else if (k % 2 == 1)
                        y += 8.0 / (MathConstants<double>::pi * MathConstants<double>::pi * k * k)
                               * std::cos (MathConstants<double>::twoPi * k * ph);
                }
                return y;
            };

            for (int wave : { (int) WaveSaw, (int) WaveTriangle })
            {
                double errBl = 0.0, errNaive = 0.0;
                for (int i = 0; i < 4000; ++i)
                {
                    const double ph = std::fmod (0.37 + i * dt, 1.0);
                    const double target = ideal (wave, ph);
                    const float bl = renderWave (wave, (float) ph, (float) dt, 0.5f);
                    const float naive = wave == WaveSaw ? (float) (2.0 * ph - 1.0) : (float) (2.0 * std::abs (2.0 * ph - 1.0) - 1.0);
                    errBl += (bl - target) * (bl - target);
                    errNaive += (naive - target) * (naive - target);
                }
                logMessage ("wave " + String (wave) + ": band-limited error " + String (std::sqrt (errBl / 4000), 5)
                            + ", naive " + String (std::sqrt (errNaive / 4000), 5));
                expectLessThan (errBl, errNaive * (wave == WaveSaw ? 0.7 : 0.95), "wave " + String (wave));
            }
        }
    }
};

//==============================================================================
class FilterTests : public UnitTest
{
public:
    FilterTests() : UnitTest ("Filters", "DSP") {}

    void runTest() override
    {
        beginTest ("LR4 crossover bands add up to an all-pass (flat energy)");
        {
            LR4Crossover a, b;
            const float g = svfG (120.0f, (float) kRate);
            Random rng (7);
            double inE = 0.0, outE = 0.0;
            for (int i = 0; i < 200000; ++i)
            {
                const float x = rng.nextFloat() * 2.0f - 1.0f;
                const float y = a.lowPass (x, g) + b.highPass (x, g);
                inE += x * x;
                outE += y * y;
            }
            expectWithinAbsoluteError (outE / inE, 1.0, 0.02);
        }

        beginTest ("Analog filter is stable at full resonance under fast cutoff sweeps");
        for (int type = 0; type < 3; ++type)
            for (int stages = 1; stages <= 3; ++stages)
            {
                AnalogFilter f;
                f.setMode (type, stages, 1.0f);
                Random rng (11);
                float peak = 0.0f;
                for (int i = 0; i < 96000; ++i)
                {
                    const float hz = 30.0f * std::pow (600.0f, 0.5f + 0.5f * std::sin ((float) i * 0.002f));
                    const float x = (rng.nextFloat() * 2.0f - 1.0f) * 1.5f;
                    const float y = f.process (x, svfG (hz, (float) kRate));
                    expect (std::isfinite (y));
                    peak = jmax (peak, std::abs (y));
                }
                expectLessThan (peak, 12.0f, "type " + String (type) + " stages " + String (stages));
            }
    }
};

//==============================================================================
class ReeseTests : public UnitTest
{
public:
    ReeseTests() : UnitTest ("Reese", "Plugin") {}

    void runTest() override
    {
        beginTest ("Phase lock keeps the sub steady (no beating) while the detune stays above it");
        {
            auto measure = [] (float lockHz)
            {
                auto p = makeProcessor();
                setParam (*p, ParamIDs::phaseLock, lockHz);
                setParam (*p, ParamIDs::monoBass, 0.0f);
                setParam (*p, ParamIDs::cutoff, 100.0f);
                setParam (*p, ParamIDs::fltEnv, 0.0f);
                setParam (*p, oscParam (0, ParamIDs::fine), -25.0f);
                setParam (*p, oscParam (1, ParamIDs::fine), 25.0f);
                setParam (*p, oscParam (0, ParamIDs::voices), 1);
                setParam (*p, oscParam (1, ParamIDs::voices), 1);
                setParam (*p, oscParam (0, ParamIDs::octave), 0);
                setParam (*p, oscParam (1, ParamIDs::octave), 0);
                const auto audio = renderNote (*p, 33, 3.0);        // A1 = 55 Hz
                auto rms = lowBandRms (audio, 70.0f, 2400);           // 50 ms windows
                rms.erase (rms.begin(), rms.begin() + 4);             // skip the attack
                const auto [mn, mx] = std::minmax_element (rms.begin(), rms.end());
                return *mx / jmax (1.0e-6f, *mn);
            };

            const float free = measure (0.0f), locked = measure (200.0f);
            logMessage ("sub level max/min: without lock " + String (free, 2) + ", with lock " + String (locked, 2));
            expectGreaterThan (free, 3.0f, "the detuned pair should beat in the sub without the lock");
            expectLessThan (locked, 1.3f, "the sub should stay steady with the lock");
        }

        beginTest ("Mono bass: below the crossover the output is mono even at 200 % spread");
        {
            auto p = makeProcessor();
            setParam (*p, ParamIDs::spread, 200.0f);
            setParam (*p, ParamIDs::monoBass, 200.0f);
            setParam (*p, oscParam (0, ParamIDs::pan), -100.0f);
            setParam (*p, oscParam (1, ParamIDs::pan), 100.0f);
            setParam (*p, oscParam (1, ParamIDs::fine), 30.0f);
            const auto audio = renderNote (*p, 28, 2.0);

            AudioBuffer<float> side (2, audio.getNumSamples());
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const float s = 0.5f * (audio.getSample (0, i) - audio.getSample (1, i));
                side.setSample (0, i, s);
                side.setSample (1, i, s);
            }
            const auto lowSide = lowBandRms (side, 50.0f, 48000);
            const auto lowAll  = lowBandRms (audio, 50.0f, 48000);
            logMessage ("low side " + String (lowSide.back(), 5) + ", low mid " + String (lowAll.back(), 5));
            expectLessThan (lowSide.back(), lowAll.back() * 0.1f);
        }

        beginTest ("Spread 0 % is mono");
        {
            auto p = makeProcessor();
            setParam (*p, ParamIDs::spread, 0.0f);
            setParam (*p, oscParam (0, ParamIDs::pan), -80.0f);
            setParam (*p, oscParam (0, ParamIDs::voices), 5);
            const auto audio = renderNote (*p, 40, 0.5);
            float diff = 0.0f;
            for (int i = 4800; i < audio.getNumSamples(); ++i)
                diff = jmax (diff, std::abs (audio.getSample (0, i) - audio.getSample (1, i)));
            expectLessThan (diff, 1.0e-4f);
        }

        beginTest ("Retrig makes every note start the same way");
        {
            auto p = makeProcessor();
            setParam (*p, ParamIDs::retrig, 1.0f);
            setParam (*p, oscParam (0, ParamIDs::voices), 4);
            const auto a = renderNote (*p, 36, 0.6, 0.2);
            const auto b = renderNote (*p, 36, 0.6, 0.2);
            float diff = 0.0f;
            for (int i = 0; i < 4000; ++i)
                diff = jmax (diff, std::abs (a.getSample (0, i) - b.getSample (0, i)));
            expectLessThan (diff, 1.0e-3f);
        }

        beginTest ("Every preset renders cleanly in every mode");
        {
            auto p = makeProcessor();
            for (int preset = 0; preset < p->getNumPrograms(); ++preset)
            {
                p->setCurrentProgram (preset);
                p->reset();
                const auto audio = renderNote (*p, 31, 1.5, 1.0);
                expect (allFinite (audio), p->getProgramName (preset));
                const float peak = audio.getMagnitude (0, audio.getNumSamples());
                logMessage (String (p->getProgramName (preset)) + ": peak " + String (peak, 3));
                expectGreaterThan (peak, 0.02f, p->getProgramName (preset));
                expectLessThan (peak, 4.0f, p->getProgramName (preset));
            }
        }

        beginTest ("Saturation, all types, stays finite and keeps the latency");
        {
            auto p = makeProcessor();
            const int latency = p->getLatencySamples();
            setParam (*p, ParamIDs::satOn, 1.0f);
            setParam (*p, ParamIDs::satDrive, 36.0f);
            for (int type = 0; type < 6; ++type)
            {
                setParam (*p, ParamIDs::satType, (float) type);
                expect (allFinite (renderNote (*p, 40, 0.3, 0.2)));
            }
            expectEquals (p->getLatencySamples(), latency);
        }

        beginTest ("Poly mode plays 8 notes and steals cleanly");
        {
            auto p = makeProcessor();
            setParam (*p, ParamIDs::playMode, (float) ModePoly);
            AudioBuffer<float> buffer (2, 256);
            for (int block = 0; block < 100; ++block)
            {
                MidiBuffer midi;
                if (block < 12)
                    midi.addEvent (MidiMessage::noteOn (1, 40 + block, 0.8f), 10);
                p->processBlock (buffer, midi);
                expect (allFinite (buffer));
            }
            expectEquals (p->gui.activeVoices.load(), kMaxPoly);
        }

        beginTest ("Synced envelopes follow the tempo (1/512 .. 1/1)");
        {
            expectWithinAbsoluteError (syncDivisionToMs (9, 120.0), 2000.0f, 0.01f);      // 1/1 at 120 = one bar
            expectWithinAbsoluteError (syncDivisionToMs (0, 120.0), 2000.0f / 512.0f, 0.001f);
            expectWithinAbsoluteError (syncDivisionToMs (5, 60.0), 250.0f, 0.01f);         // 1/16 at 60 BPM = a quarter beat

            // amp attack synced to 1/4 at 120 BPM (no host tempo here): the level reaches its peak after ~500 ms
            auto p = makeProcessor();
            setParam (*p, ParamIDs::aSync, 1.0f);
            setParam (*p, ParamIDs::aAttackSync, 7.0f);   // 1/4
            setParam (*p, ParamIDs::aSustain, 100.0f);
            setParam (*p, ParamIDs::satOn, 0.0f);
            const auto audio = renderNote (*p, 40, 1.0);
            auto rms = [&] (int from, int len)
            {
                double acc = 0.0;
                for (int i = from; i < from + len; ++i)
                    acc += (double) audio.getSample (0, i) * audio.getSample (0, i);
                return std::sqrt (acc / len);
            };
            const double at250 = rms (12000 - 1200, 2400), at750 = rms (36000 - 1200, 2400);
            logMessage ("attack 1/4: level at 250 ms " + String (at250, 3) + ", at 750 ms " + String (at750, 3));
            expectLessThan (at250, at750 * 0.75);
            expectGreaterThan (at250, at750 * 0.3);
        }

        beginTest ("State round trip");
        {
            auto p = makeProcessor();
            setParam (*p, ParamIDs::cutoff, 33.3f);
            setParam (*p, oscParam (1, ParamIDs::octave), 2);
            MemoryBlock state;
            p->getStateInformation (state);
            auto q = makeProcessor();
            q->setStateInformation (state.getData(), (int) state.getSize());
            expectWithinAbsoluteError (q->apvts.getRawParameterValue (ParamIDs::cutoff)->load(), 33.3f, 0.01f);
            expectEquals ((int) q->apvts.getRawParameterValue (oscParam (1, ParamIDs::octave))->load(), 2);
        }
    }
};

static OscillatorTests oscillatorTests;
static FilterTests filterTests;
static ReeseTests reeseTests;

