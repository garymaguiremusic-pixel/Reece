#include "SynthEngine.h"
#include <cmath>
#include <algorithm>

namespace rs
{
    namespace
    {
        constexpr int kSubBlock = 16;                 // pitch / glide update rate
        constexpr float kCutoffSemisPerStep = 1.19589258f;   // cutoff = 20 Hz * 1000^(v/100): semitones per knob step
        constexpr float kVoiceGain = 0.35f;           // headroom for 2 oscillators x 8 unison voices

        inline float semisToHz (float semis) noexcept   { return 440.0f * std::exp2 ((semis - 69.0f) * (1.0f / 12.0f)); }

        /** Deterministic unison phase offsets, so a retriggered stack starts the same way every note. */
        inline float unisonPhaseOffset (int i) noexcept   { return i == 0 ? 0.0f : wrap01 ((float) i * 0.381966f); }
    }

    //==============================================================================
    // Voice
    //==============================================================================
    void Voice::prepare (double sr, std::uint32_t seed)
    {
        sampleRate = sr;
        invSampleRate = (float) (1.0 / sr);
        ampEnv.setSampleRate (sr);
        fltEnv.setSampleRate (sr);
        random.seed (seed * 2654435761u + 1u);
        for (int o = 0; o < kNumOscs; ++o)
            noise[o].seed (seed * 747796405u + (std::uint32_t) o * 2891336453u + 7u);
        drift = random.next() * 0.12f;     // +- 0.12 semitones
        reset();
    }

    void Voice::reset()
    {
        ampEnv.reset();
        fltEnv.reset();
        filterL.reset();
        filterR.reset();
        xoverL.reset();
        xoverR.reset();
        xoverLock.reset();
        pending = {};
        fadeGain = 1.0f;
        fadeStep = 0.0f;
        held = false;
    }

    bool Voice::willResetPhases (const EngineSettings& s) const noexcept
    {
        if (s.phaseLockHz >= 1.0f)
            return true;
        for (const auto& os : s.osc)
            if (os.retrig && os.gain > 0.0f)
                return true;
        return false;
    }

    void Voice::start (int newNote, float vel, bool glideFromCurrent, const EngineSettings& s)
    {
        held = true;

        // A sounding voice whose phases are about to jump gets a 3 ms fade first.
        if (ampEnv.isActive() && ampEnv.getLevel() > 0.001f && willResetPhases (s))
        {
            pending.waiting = true;
            pending.releaseAfter = false;
            pending.glide = glideFromCurrent;
            pending.note = newNote;
            pending.velocity = vel;
            fadeStep = 1.0f / (float) std::max (1.0, 0.003 * sampleRate);
            return;
        }

        doStart (newNote, vel, glideFromCurrent, s);
    }

    void Voice::doStart (int newNote, float vel, bool glideFromCurrent, const EngineSettings& s)
    {
        const bool wasSilent = ! ampEnv.isActive() || ampEnv.getLevel() <= 0.001f;

        note = newNote;
        velocity = vel;
        targetPitch = (float) newNote;
        if (! glideFromCurrent || wasSilent || s.glideMs <= 0.0f)
            pitch = targetPitch;

        for (int o = 0; o < kNumOscs; ++o)
        {
            const auto& os = s.osc[o];
            if (os.retrig)
            {
                for (int i = 0; i < kMaxUnison; ++i)
                    phases[o][i] = wrap01 (os.phase + unisonPhaseOffset (i));
            }
            else if (wasSilent)
            {
                // free running: a fresh voice starts each unison voice at a random phase,
                // a sounding voice just keeps running
                for (int i = 0; i < kMaxUnison; ++i)
                    phases[o][i] = wrap01 (random.next());
            }
            // the phase-locked low band always restarts in phase
            lockPhase[o] = wrap01 (os.phase);
        }

        if (wasSilent)
        {
            filterL.reset();
            filterR.reset();
            xoverL.reset();
            xoverR.reset();
            xoverLock.reset();
        }

        ampEnv.noteOn();
        fltEnv.noteOn();
    }

    void Voice::legatoTo (int newNote, const EngineSettings& s)
    {
        if (pending.waiting)
        {
            pending.note = newNote;
            return;
        }
        held = true;
        note = newNote;
        targetPitch = (float) newNote;
        if (s.glideMs <= 0.0f)
            pitch = targetPitch;
    }

    void Voice::release()
    {
        held = false;
        if (pending.waiting)
        {
            pending.releaseAfter = true;
            return;
        }
        ampEnv.noteOff();
        fltEnv.noteOff();
    }

    void Voice::render (float* left, float* right, int numSamples, const EngineSettings& s, const ChunkShared& sh) noexcept
    {
        if (! isActive())
            return;

        ampEnv.setParameters (s.aA, s.aD, s.aS, s.aR);
        fltEnv.setParameters (s.fA, s.fD, s.fS, s.fR);
        filterL.setMode (s.filterType, s.filterStages, sh.resonance);
        filterR.setMode (s.filterType, s.filterStages, sh.resonance);

        const float glideCoef = s.glideMs > 0.0f
                                  ? 1.0f - std::exp (-(float) kSubBlock / (s.glideMs * 0.001f * (float) sampleRate))
                                  : 1.0f;
        const float sr = (float) sampleRate;

        for (int start = 0; start < numSamples; start += kSubBlock)
        {
            const int end = std::min (numSamples, start + kSubBlock);

            // pitch for this sub-block
            pitch += (targetPitch - pitch) * glideCoef;
            const float p = pitch + sh.bendSemis;

            float baseInc[kNumOscs], lockInc[kNumOscs];
            for (int o = 0; o < kNumOscs; ++o)
            {
                const auto& os = s.osc[o];
                baseInc[o] = semisToHz (p + os.pitchSemis + os.fineCents * 0.01f) * invSampleRate;
                lockInc[o] = semisToHz (p + os.pitchSemis + sh.lockFineCents * 0.01f) * invSampleRate;
            }
            const float keySemis = s.keyTrack * (p - 60.0f) + drift;

            for (int i = start; i < end; ++i)
            {
                // de-click fade, then the pending restart
                if (pending.waiting)
                {
                    fadeGain -= fadeStep;
                    if (fadeGain <= 0.0f)
                    {
                        fadeGain = 1.0f;
                        ampEnv.reset();
                        fltEnv.reset();
                        const auto pd = pending;
                        pending = {};
                        doStart (pd.note, pd.velocity, pd.glide, s);
                        if (pd.releaseAfter)
                            release();
                    }
                }

                const float amp = ampEnv.next();
                const float fenv = fltEnv.next();

                // oscillators: detuned stereo stack + one coherent mono copy for the low band
                float dl = 0.0f, dr = 0.0f, lock = 0.0f;
                for (int o = 0; o < kNumOscs; ++o)
                {
                    const auto& os = s.osc[o];
                    if (os.gain <= 0.0f)
                        continue;

                    if (os.wave == WaveNoise)
                    {
                        const float n = noise[o].next();
                        dl += n * sh.noiseL[o];
                        dr += n * sh.noiseR[o];
                        lock += n * os.gain;
                        continue;
                    }

                    const int nv = sh.numVoices[o];
                    for (int v = 0; v < nv; ++v)
                    {
                        const float inc = baseInc[o] * sh.ratio[o][v];
                        float& ph = phases[o][v];
                        const float y = renderWave (os.wave, ph, inc, os.pw);
                        ph += inc;
                        if (ph >= 1.0f) ph -= 1.0f;
                        dl += y * sh.gainL[o][v];
                        dr += y * sh.gainR[o][v];
                    }

                    float& lp = lockPhase[o];
                    lock += renderWave (os.wave, lp, lockInc[o], os.pw) * os.gain;
                    lp += lockInc[o];
                    if (lp >= 1.0f) lp -= 1.0f;
                }

                // Phase lock: below the crossover the coherent copy replaces the detuned stack,
                // so the sub never beats or cancels; above it the full Reese movement stays.
                const float lg = sh.lockG[i];
                if (lg > 0.0f)
                {
                    const float low = xoverLock.lowPass (lock, lg);
                    dl = xoverL.highPass (dl, lg) + low;
                    dr = xoverR.highPass (dr, lg) + low;
                }

                // analog filter, cutoff computed per sample (no steps)
                const float semis = sh.cutoffSemis[i] + keySemis + fenv * s.envSemis;
                const float hz = 20.0f * std::exp2 (semis * (1.0f / 12.0f));
                const float g = svfG (hz, sr);
                const float yl = filterL.process (dl, g);
                const float yr = filterR.process (dr, g);

                const float gain = amp * fadeGain * kVoiceGain * velocity;
                left[i]  += yl * gain;
                right[i] += yr * gain;
            }
        }

        if (! isActive())
            held = false;
    }

    //==============================================================================
    // SynthEngine
    //==============================================================================
    void SynthEngine::prepare (double sr)
    {
        sampleRate = sr;
        for (int v = 0; v < kMaxPoly; ++v)
            voices[v].prepare (sr, (std::uint32_t) (v + 1) * 0x51ed27u);
        smoothCoef = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));    // 20 ms
        reset();
    }

    void SynthEngine::reset()
    {
        for (auto& v : voices)
            v.reset();
        stackSize = 0;
        firstBlock = true;
    }

    void SynthEngine::setSettings (const EngineSettings& s)
    {
        if (lastPlayMode >= 0 && s.playMode != lastPlayMode)
            allNotesOff();
        lastPlayMode = s.playMode;
        settings = s;

        if (firstBlock)
        {
            cutoffSm = s.cutoff;
            resSm = s.resonance;
            lockSm = s.phaseLockHz;
            firstBlock = false;
        }
        prepareShared();
    }

    void SynthEngine::prepareShared() noexcept
    {
        float fineSum = 0.0f;
        int fineCount = 0;

        for (int o = 0; o < kNumOscs; ++o)
        {
            const auto& os = settings.osc[o];
            const int n = std::clamp (os.voices, 1, kMaxUnison);
            shared.numVoices[o] = n;

            const float norm = os.gain / std::sqrt ((float) n);
            for (int v = 0; v < n; ++v)
            {
                // symmetric spread: -1 .. +1 across the stack
                const float pos = n == 1 ? 0.0f : 2.0f * (float) v / (float) (n - 1) - 1.0f;
                shared.ratio[o][v] = std::exp2 (pos * os.detuneCents / 1200.0f);

                // unison voices fan out across the stereo field around the oscillator's pan
                const float pan = std::clamp (os.pan + pos * (1.0f - std::abs (os.pan)), -1.0f, 1.0f);
                const float angle = (pan + 1.0f) * 0.785398163f;
                shared.gainL[o][v] = std::cos (angle) * 1.41421356f * norm;
                shared.gainR[o][v] = std::sin (angle) * 1.41421356f * norm;
            }

            const float angle = (std::clamp (os.pan, -1.0f, 1.0f) + 1.0f) * 0.785398163f;
            shared.noiseL[o] = std::cos (angle) * 1.41421356f * os.gain;
            shared.noiseR[o] = std::sin (angle) * 1.41421356f * os.gain;

            if (os.gain > 0.0f && os.wave != WaveNoise)
            {
                fineSum += os.fineCents;
                ++fineCount;
            }
        }

        // the locked low band sits in the middle of the two oscillators' fine tuning
        shared.lockFineCents = fineCount > 0 ? fineSum / (float) fineCount : 0.0f;
        shared.bendSemis = bendSemis;
    }

    void SynthEngine::pitchWheel (int value14bit)
    {
        bendSemis = 2.0f * ((float) value14bit - 8192.0f) / 8192.0f;
        shared.bendSemis = bendSemis;
    }

    Voice* SynthEngine::findVoiceToUse() noexcept
    {
        Voice* best = nullptr;
        for (auto& v : voices)
            if (! v.isActive())
                return &v;

        // steal: prefer the oldest releasing voice, then the oldest voice
        for (auto& v : voices)
            if (v.isReleasing() && (best == nullptr || v.getAge() < best->getAge()))
                best = &v;
        if (best != nullptr)
            return best;

        for (auto& v : voices)
            if (best == nullptr || v.getAge() < best->getAge())
                best = &v;
        return best;
    }

    void SynthEngine::noteOn (int note, float velocity)
    {
        // velocity has a gentle effect (bass lines stay even)
        const float vel = 0.75f + 0.25f * std::clamp (velocity, 0.0f, 1.0f);

        if (settings.playMode == ModePoly)
        {
            // the same note again restarts its voice
            Voice* voice = nullptr;
            for (auto& v : voices)
                if (v.isActive() && v.getNote() == note)
                    voice = &v;
            if (voice == nullptr)
                voice = findVoiceToUse();

            voice->setAge (++ageCounter);
            voice->start (note, vel, false, settings);
            return;
        }

        // Mono / Legato: one voice, last-note priority
        const bool othersHeld = stackSize > 0;
        for (int i = 0; i < stackSize; ++i)
            if (noteStack[i] == note)
            {
                std::move (noteStack + i + 1, noteStack + stackSize, noteStack + i);
                --stackSize;
                break;
            }
        if (stackSize < (int) std::size (noteStack))
            noteStack[stackSize++] = note;

        auto& v = voices[0];
        if (settings.playMode == ModeLegato && othersHeld && v.isActive())
            v.legatoTo (note, settings);
        else
            v.start (note, vel, v.isActive(), settings);
    }

    void SynthEngine::noteOff (int note)
    {
        if (settings.playMode == ModePoly)
        {
            for (auto& v : voices)
                if (v.isHeld() && v.getNote() == note)
                    v.release();
            return;
        }

        bool wasTop = stackSize > 0 && noteStack[stackSize - 1] == note;
        for (int i = 0; i < stackSize; ++i)
            if (noteStack[i] == note)
            {
                std::move (noteStack + i + 1, noteStack + stackSize, noteStack + i);
                --stackSize;
                break;
            }

        auto& v = voices[0];
        if (stackSize == 0)
            v.release();
        else if (wasTop)
            v.legatoTo (noteStack[stackSize - 1], settings);   // back to the note still held
    }

    void SynthEngine::allNotesOff()
    {
        stackSize = 0;
        for (auto& v : voices)
            if (v.isActive())
                v.release();
    }

    int SynthEngine::getNumActiveVoices() const noexcept
    {
        int n = 0;
        for (auto& v : voices)
            n += v.isActive() ? 1 : 0;
        return n;
    }

    void SynthEngine::render (float* left, float* right, int numSamples) noexcept
    {
        for (int pos = 0; pos < numSamples; pos += ChunkShared::kMax)
            renderChunk (left + pos, right + pos, std::min (ChunkShared::kMax, numSamples - pos));
    }

    void SynthEngine::renderChunk (float* left, float* right, int numSamples) noexcept
    {
        // smoothed controls, per sample: the cutoff never steps
        const float sr = (float) sampleRate;
        for (int i = 0; i < numSamples; ++i)
        {
            cutoffSm += (settings.cutoff - cutoffSm) * smoothCoef;
            lockSm += (settings.phaseLockHz - lockSm) * smoothCoef;
            shared.cutoffSemis[i] = cutoffSm * kCutoffSemisPerStep;
            shared.lockG[i] = lockSm >= 1.0f ? svfG (lockSm, sr) : 0.0f;
        }
        resSm += (settings.resonance - resSm) * std::min (1.0f, smoothCoef * (float) numSamples);
        shared.resonance = resSm;

        for (auto& v : voices)
            v.render (left, right, numSamples, settings, shared);
    }
}
