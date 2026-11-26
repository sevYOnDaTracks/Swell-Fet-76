#include "FetProcessor.h"
#include "FetEditor.h"

using namespace juce;

FetProcessor::Layout FetProcessor::createLayout()
{
    Layout l;
    auto db = [] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; };
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input", NormalisableRange<float> (-20.0f, 40.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-30.0f, 20.0f, 0.1f), 0.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction (db)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "attack", 1 }, "Attack", NormalisableRange<float> (1.0f, 7.0f, 0.01f), 3.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                  { return String (v, 1) + "  (" + String (swell::FetEngine::attackSeconds (v) * 1.0e6f, 0) + " us)"; })));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "release", 1 }, "Release", NormalisableRange<float> (1.0f, 7.0f, 0.01f), 5.0f,
                                                  AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                                  { return String (v, 1) + "  (" + String (swell::FetEngine::releaseSeconds (v) * 1000.0f, 0) + " ms)"; })));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "ratio", 1 }, "Ratio", StringArray { "4:1", "8:1", "12:1", "20:1", "All" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "meter", 1 }, "Meter", StringArray { "GR", "+8", "+4", "Off" }, 0));
    addCommonParameters (l);
    return l;
}

FetProcessor::FetProcessor() : AnalogProcessorBase (createLayout())
{
    input = apvts.getRawParameterValue ("input");
    output = apvts.getRawParameterValue ("output");
    attack = apvts.getRawParameterValue ("attack");
    release = apvts.getRawParameterValue ("release");
    ratio = apvts.getRawParameterValue ("ratio");
}

// Presets d'usine, calculés et vérifiés par Tests/PresetTest.cpp (même niveau en sortie, réduction visée,
// caractère du nom) pour des pistes qui culminent vers -6 dBFS. Ratio : 0 = 4:1, 1 = 8:1, 2 = 12:1, 3 = 20:1, 4 = All. Attack / Release : 7 = le plus rapide.
const std::vector<FetProcessor::FactoryPreset>& FetProcessor::factoryPresets() const
{
    static const std::vector<FactoryPreset> list {
        { "Init",        "Default",               {} },
        { "Vocals", "Vocal Smooth", { { "input", 8.5 }, { "output", -2.5 }, { "attack", 3 }, { "release", 5 }, { "ratio", 0 } } },
        { "Vocals", "Vocal Upfront", { { "input", 11.5 }, { "output", -2 }, { "attack", 5 }, { "release", 6 }, { "ratio", 1 } } },
        { "Vocals", "Vocal Aggressive", { { "input", 15.5 }, { "output", -2 }, { "attack", 6 }, { "release", 7 }, { "ratio", 3 } } },
        { "Vocals", "Vocal Parallel Grit", { { "input", 18 }, { "output", 3.5 }, { "attack", 6 }, { "release", 6 }, { "ratio", 4 }, { "mix", 40 } } },
        { "Vocals", "Vocal Peak Catcher", { { "input", 1.5 }, { "output", 1 }, { "attack", 7 }, { "release", 5 }, { "ratio", 3 } } },
        { "Drums", "Kick Punch", { { "input", 11.5 }, { "output", -3.5 }, { "attack", 1 }, { "release", 6 }, { "ratio", 0 } } },
        { "Drums", "Snare Crack", { { "input", 15.5 }, { "output", -4.5 }, { "attack", 2 }, { "release", 7 }, { "ratio", 0 } } },
        { "Drums", "Drum Bus Punch", { { "input", 11.5 }, { "output", -2.5 }, { "attack", 2 }, { "release", 7 }, { "ratio", 0 } } },
        { "Drums", "Drum Bus Parallel", { { "input", 22 }, { "output", 6.5 }, { "attack", 6 }, { "release", 7 }, { "ratio", 4 }, { "mix", 40 } } },
        { "Drums", "Room Smash", { { "input", 22 }, { "output", 3.5 }, { "attack", 7 }, { "release", 7 }, { "ratio", 4 } } },
        { "Bass", "Bass Even", { { "input", 5 }, { "output", 0.5 }, { "attack", 3 }, { "release", 3 }, { "ratio", 0 } } },
        { "Bass", "Bass Grip", { { "input", 9 }, { "output", 0 }, { "attack", 5 }, { "release", 4 }, { "ratio", 1 } } },
        { "Bass", "808 Control", { { "input", 3 }, { "output", 3.5 }, { "attack", 3 }, { "release", 2 }, { "ratio", 1 } } },
        { "Instruments", "Guitar Sustain", { { "input", 18.5 }, { "output", -5.5 }, { "attack", 3 }, { "release", 7 }, { "ratio", 1 } } },
        { "Instruments", "Acoustic Guitar", { { "input", 10 }, { "output", -3.5 }, { "attack", 2 }, { "release", 4 }, { "ratio", 0 } } },
        { "Instruments", "Piano Gentle", { { "input", 6 }, { "output", -1 }, { "attack", 2 }, { "release", 3 }, { "ratio", 0 } } },
        { "Color", "All Buttons Crush", { { "input", 20 }, { "output", 4.5 }, { "attack", 6 }, { "release", 7 }, { "ratio", 4 } } },
    };
    return list;
}

float FetProcessor::processEngine (dsp::AudioBlock<float>& block)
{
    engine.setParameters (input->load(), output->load(), attack->load(), release->load(), (int) ratio->load());
    return engine.process (block);
}

AudioProcessorEditor* FetProcessor::createEditor() { return swell::LicenceGuard::attach (new FetEditor (*this)); }

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FetProcessor(); }
