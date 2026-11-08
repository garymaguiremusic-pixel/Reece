#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace juce;
using namespace rs;

//==============================================================================
ReeseAudioProcessor::ReeseAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ReeseState", createParameterLayout())
{
}

bool ReeseAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == AudioChannelSet::stereo();
}

void ReeseAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate);
    saturation.prepare (sampleRate, jmax (1, samplesPerBlock), 2);
    setLatencySamples (saturation.getLatencySamples());

    spreadSmooth.reset (sampleRate, 0.03);
    volumeSmooth.reset (sampleRate, 0.03);
    monoHzSmooth.reset (sampleRate, 0.03);
    reset();
}

void ReeseAudioProcessor::reset()
{
    // jump straight to the current settings (no ramps left over from before)
    spreadSmooth.setCurrentAndTargetValue (raw (ParamIDs::spread) * 0.01f);
    const float volDb = raw (ParamIDs::volume);
    volumeSmooth.setCurrentAndTargetValue (volDb <= -47.9f ? 0.0f : Decibels::decibelsToGain (volDb));
    monoHzSmooth.setCurrentAndTargetValue (raw (ParamIDs::monoBass));

    engine.reset();
    saturation.reset();
    monoXoverL.reset();
    monoXoverR.reset();
}

//==============================================================================
EngineSettings ReeseAudioProcessor::makeEngineSettings() const
{
    EngineSettings s;
    for (int o = 0; o < kNumOscs; ++o)
    {
        auto& os = s.osc[o];
        os.wave        = jlimit (0, NumWaves - 1, (int) raw (oscParam (o, ParamIDs::wave)));
        os.pitchSemis  = 12.0f * (float) roundToInt (raw (oscParam (o, ParamIDs::octave))) + raw (oscParam (o, ParamIDs::semi));
        os.fineCents   = raw (oscParam (o, ParamIDs::fine));
        os.voices      = jlimit (1, kMaxUnison, roundToInt (raw (oscParam (o, ParamIDs::voices))));
        os.detuneCents = raw (oscParam (o, ParamIDs::detune));
        os.pw          = raw (oscParam (o, ParamIDs::pw)) * 0.01f;
        os.phase       = raw (oscParam (o, ParamIDs::phase)) / 360.0f;
        os.retrig      = raw (oscParam (o, ParamIDs::retrig)) > 0.5f;
        os.pan         = raw (oscParam (o, ParamIDs::pan)) * 0.01f;
        os.gain        = raw (oscParam (o, ParamIDs::level)) * 0.01f;
    }

    s.playMode    = jlimit (0, 2, (int) raw (ParamIDs::playMode));
    s.glideMs     = glideToMs (raw (ParamIDs::glide));
    s.phaseLockHz = raw (ParamIDs::phaseLock);

    s.filterType   = jlimit (0, 2, (int) raw (ParamIDs::fltType));
    s.filterStages = jlimit (1, 3, (int) raw (ParamIDs::fltSlope) + 1);
    s.cutoff       = raw (ParamIDs::cutoff);
    s.resonance    = raw (ParamIDs::resonance) * 0.01f;
    s.envSemis     = fltEnvToSemis (raw (ParamIDs::fltEnv));
    s.keyTrack     = raw (ParamIDs::keyTrack) * 0.01f;

    // Attack / Decay / Release: free times, or note values at the host tempo when Sync is on
    auto envTime = [this] (const char* freeId, const char* syncId, bool synced)
    {
        return synced ? syncDivisionToMs (roundToInt (raw (syncId)), bpm) : envTimeToMs (raw (freeId));
    };
    const bool fSync = raw (ParamIDs::fSync) > 0.5f, aSync = raw (ParamIDs::aSync) > 0.5f;
    s.fA = envTime (ParamIDs::fAttack,  ParamIDs::fAttackSync,  fSync);
    s.fD = envTime (ParamIDs::fDecay,   ParamIDs::fDecaySync,   fSync);
    s.fS = raw (ParamIDs::fSustain) * 0.01f;
    s.fR = envTime (ParamIDs::fRelease, ParamIDs::fReleaseSync, fSync);
    s.aA = envTime (ParamIDs::aAttack,  ParamIDs::aAttackSync,  aSync);
    s.aD = envTime (ParamIDs::aDecay,   ParamIDs::aDecaySync,   aSync);
    s.aS = raw (ParamIDs::aSustain) * 0.01f;
    s.aR = envTime (ParamIDs::aRelease, ParamIDs::aReleaseSync, aSync);
    return s;
}

SaturationSettings ReeseAudioProcessor::makeSaturationSettings() const
{
    SaturationSettings s;
    s.on      = raw (ParamIDs::satOn) > 0.5f;
    s.type    = jlimit (0, 5, (int) raw (ParamIDs::satType));
    s.driveDb = raw (ParamIDs::satDrive);
    s.tone    = raw (ParamIDs::satTone) * 0.01f;
    s.mix     = raw (ParamIDs::satMix) * 0.01f;
    s.levelDb = raw (ParamIDs::satLevel);
    return s;
}

//==============================================================================
void ReeseAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int num = buffer.getNumSamples();
    buffer.clear();
    if (num == 0 || buffer.getNumChannels() < 2)
        return;

    // host tempo for the synced envelopes (120 BPM when the host gives none)
    if (auto* ph = getPlayHead())
        if (auto position = ph->getPosition())
            if (auto hostBpm = position->getBpm())
                if (*hostBpm > 1.0)
                    bpm = *hostBpm;
    gui.bpm.store ((float) bpm, std::memory_order_relaxed);

    engine.setSettings (makeEngineSettings());

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    // render between MIDI events, so notes start on the exact sample
    int pos = 0;
    for (const auto metadata : midi)
    {
        const int eventPos = jlimit (0, num, metadata.samplePosition);
        if (eventPos > pos)
        {
            engine.render (left + pos, right + pos, eventPos - pos);
            pos = eventPos;
        }

        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
            engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            engine.noteOff (msg.getNoteNumber());
        else if (msg.isPitchWheel())
            engine.pitchWheel (msg.getPitchWheelValue());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine.allNotesOff();
    }
    if (pos < num)
        engine.render (left + pos, right + pos, num - pos);

    processOutput (buffer);
    gui.activeVoices.store (engine.getNumActiveVoices(), std::memory_order_relaxed);
}

void ReeseAudioProcessor::processOutput (AudioBuffer<float>& buffer) noexcept
{
    const int num = buffer.getNumSamples();

    // 1. Saturation (the KickDrum design: 4x oversampled, constant latency)
    saturation.process (buffer, makeSaturationSettings());

    // 2. Stereo spread (mid/side: 0 % = mono, 100 % = as played, 200 % = double width)
    // 3. Mono bass: everything below the crossover is summed to mono, so the lows stay
    //    centred and in phase even at 200 % spread.
    // 4. Volume
    spreadSmooth.setTargetValue (raw (ParamIDs::spread) * 0.01f);
    monoHzSmooth.setTargetValue (raw (ParamIDs::monoBass));
    const float volDb = raw (ParamIDs::volume);
    volumeSmooth.setTargetValue (volDb <= -47.9f ? 0.0f : Decibels::decibelsToGain (volDb));

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);
    const float sr = (float) currentSampleRate;
    float peak = 0.0f;
    int w = gui.scopeWrite.load (std::memory_order_relaxed);

    for (int i = 0; i < num; ++i)
    {
        float l = left[i], r = right[i];

        const float width = spreadSmooth.getNextValue();
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * width;
        l = mid + side;
        r = mid - side;

        const float monoHz = monoHzSmooth.getNextValue();
        if (monoHz >= 1.0f)
        {
            const float g = svfG (monoHz, sr);
            const float low = 0.5f * (monoXoverL.lowPass (l, g) + monoXoverR.lowPass (r, g));
            l = monoXoverL.highPass (l, g) + low;
            r = monoXoverR.highPass (r, g) + low;
        }
        else
        {
            monoXoverL.reset();
            monoXoverR.reset();
        }

        const float vol = volumeSmooth.getNextValue();
        l *= vol;
        r *= vol;
        left[i] = l;
        right[i] = r;

        peak = jmax (peak, std::abs (l), std::abs (r));
        gui.scopeL[w] = l;
        gui.scopeR[w] = r;
        w = (w + 1) & (GuiState::kScopeSize - 1);
    }

    gui.scopeWrite.store (w, std::memory_order_release);
    gui.peak.store (jmax (peak, gui.peak.load (std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
}

//==============================================================================
int ReeseAudioProcessor::getNumPrograms()
{
    return (int) getFactoryPresets().size();
}

const String ReeseAudioProcessor::getProgramName (int index)
{
    const auto& presets = getFactoryPresets();
    return isPositiveAndBelow (index, (int) presets.size()) ? String (presets[(size_t) index].name) : String();
}

void ReeseAudioProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (! isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram.store (index);

    // everything back to default, then the preset's own values
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<RangedAudioParameter*> (p))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
}

//==============================================================================
void ReeseAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram.load(), nullptr);
    state.setProperty ("editorWidth", gui.editorWidth.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ReeseAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = ValueTree::fromXml (*xml);
            currentProgram.store (jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0)));
            gui.editorWidth.store ((int) state.getProperty ("editorWidth", 0));
            apvts.replaceState (state);

            // replaceState skips parameters whose stored value looks unchanged, which can leave
            // a button or choice at an in-between value a host set. Apply every value explicitly.
            for (auto* p : getParameters())
                if (auto* ranged = dynamic_cast<RangedAudioParameter*> (p))
                {
                    const auto child = state.getChildWithProperty ("id", ranged->getParameterID());
                    if (child.isValid() && child.hasProperty ("value"))
                        ranged->setValueNotifyingHost (ranged->convertTo0to1 ((float) child.getProperty ("value")));
                }
        }
    }
}

//==============================================================================
AudioProcessorEditor* ReeseAudioProcessor::createEditor()
{
    return new ReeseAudioProcessorEditor (*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ReeseAudioProcessor();
}
