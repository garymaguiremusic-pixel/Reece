#pragma once

#include "Parameters.h"
#include <vector>
#include <utility>

namespace rs
{
    /** A factory preset: every parameter starts at its default, then these values are applied. */
    struct Preset
    {
        const char* name;
        std::vector<std::pair<juce::String, float>> values;
    };

    inline juce::String o1 (const char* n) { return oscParam (0, n); }
    inline juce::String o2 (const char* n) { return oscParam (1, n); }

    inline const std::vector<Preset>& getFactoryPresets()
    {
        using namespace ParamIDs;
        static const std::vector<Preset> presets =
        {
            { "Init Reese", {} },

            { "Classic Reese",
              { { o1 (fine), -9.0f }, { o2 (fine), 9.0f }, { o1 (voices), 1 }, { o2 (voices), 1 },
                { cutoff, 58.0f }, { resonance, 12.0f }, { fltEnv, 10.0f }, { monoBass, 110.0f } } },

            { "Wide Unison Reese",
              { { o1 (voices), 6 }, { o2 (voices), 6 }, { o1 (detune), 22.0f }, { o2 (detune), 26.0f },
                { o1 (fine), -6.0f }, { o2 (fine), 6.0f }, { o1 (pan), -35.0f }, { o2 (pan), 35.0f },
                { spread, 160.0f }, { monoBass, 140.0f }, { cutoff, 66.0f } } },

            { "Square Reese",
              { { o1 (wave), WaveSquare }, { o2 (wave), WavePulse }, { o2 (pw), 32.0f },
                { o1 (fine), -11.0f }, { o2 (fine), 11.0f }, { o1 (voices), 2 }, { o2 (voices), 2 },
                { cutoff, 55.0f }, { resonance, 22.0f }, { fltSlope, 2 } } },

            { "Neuro Growl",
              { { o1 (voices), 4 }, { o2 (voices), 4 }, { o1 (detune), 30.0f }, { o2 (detune), 18.0f },
                { o2 (wave), WaveSawSquare }, { o2 (semi), 0.0f }, { o2 (fine), 15.0f }, { o1 (fine), -15.0f },
                { cutoff, 52.0f }, { resonance, 45.0f }, { fltEnv, 34.0f }, { fDecay, 48.0f }, { fSustain, 20.0f },
                { satOn, 1 }, { satType, 1 }, { satDrive, 14.0f }, { monoBass, 150.0f }, { volume, -8.0f } } },

            { "Dark Sub Reese",
              { { o1 (fine), -5.0f }, { o2 (fine), 5.0f }, { o2 (octave), -1 }, { o1 (voices), 1 }, { o2 (voices), 3 },
                { cutoff, 42.0f }, { resonance, 8.0f }, { fltEnv, 6.0f }, { fltSlope, 2 },
                { monoBass, 180.0f }, { spread, 80.0f } } },

            { "Band Pass Reese",
              { { fltType, FilterBP }, { cutoff, 60.0f }, { resonance, 35.0f }, { o1 (voices), 3 }, { o2 (voices), 3 },
                { o1 (fine), -12.0f }, { o2 (fine), 12.0f }, { monoBass, 0.0f }, { volume, 6.0f } } },

            { "Tube Driven Reese",
              { { o1 (fine), -14.0f }, { o2 (fine), 14.0f }, { o1 (voices), 2 }, { o2 (voices), 2 },
                { satOn, 1 }, { satType, 2 }, { satDrive, 18.0f }, { satTone, 70.0f },
                { cutoff, 57.0f }, { resonance, 25.0f }, { glide, 45.0f }, { playMode, ModeLegato }, { satLevel, -6.0f }, { volume, -9.0f } } },

            { "FM Reese Growl",
              { { o1 (fmOn), 1 }, { o1 (fmAmount), 32.0f }, { o1 (voices), 3 }, { o1 (detune), 14.0f },
                { o2 (wave), WaveSine }, { o2 (octave), 1 }, { o2 (fine), 7.0f }, { o2 (level), 18.0f }, { o2 (voices), 1 },
                { cutoff, 64.0f }, { resonance, 20.0f }, { fltEnv, 24.0f }, { fDecay, 50.0f }, { fSustain, 30.0f },
                { monoBass, 130.0f }, { volume, -6.0f } } },

            { "Poly Reese Stab",
              { { playMode, ModePoly }, { o1 (voices), 3 }, { o2 (voices), 3 }, { o1 (octave), 0 }, { o2 (octave), 0 },
                { aDecay, 45.0f }, { aSustain, 40.0f }, { aRelease, 40.0f }, { fltEnv, 43.0f }, { fDecay, 45.0f },
                { fSustain, 10.0f }, { cutoff, 48.0f }, { resonance, 30.0f }, { monoBass, 100.0f }, { volume, -8.0f } } },
        };
        return presets;
    }
}
