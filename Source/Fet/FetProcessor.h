#pragma once
#include "Common/AnalogProcessorBase.h"
#include "FetEngine.h"

class FetProcessor : public swell::AnalogProcessorBase
{
public:
    FetProcessor();
    const juce::String getName() const override { return "Swell FET 76"; }

    const std::vector<FactoryPreset>& factoryPresets() const override;
    juce::String productName() const override { return getName(); }

protected:
    void prepareEngine (double rate, int channels) override { engine.prepare (rate, channels); }
    void resetEngine() override { engine.reset(); }
    float processEngine (juce::dsp::AudioBlock<float>&) override;

private:
    juce::AudioProcessorEditor* createEditor() override;
    static Layout createLayout();

    swell::FetEngine engine;
    std::atomic<float> *input, *output, *attack, *release, *ratio;
};
