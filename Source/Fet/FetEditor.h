#pragma once
#include "Common/AnalogEditor.h"
#include "FetProcessor.h"

class FetEditor : public swell::analog::AnalogEditor
{
public:
    explicit FetEditor (FetProcessor&);
    ~FetEditor() override { sliderAttachments.clear(); }   // les boutons meurent avant la classe de base
    void settleMeters() { updateMeter(); meter.jumpToTarget(); }   // captures hors ligne

private:
    void drawFacePlate (juce::Graphics&);
    void updateMeter();
    void syncKeys();

    FetProcessor& fet;
    using Knob = swell::analog::Knob;
    Knob input { Knob::Style::FetLarge }, output { Knob::Style::FetLarge };
    Knob attack { Knob::Style::FetSmall }, release { Knob::Style::FetSmall }, mix { Knob::Style::FetSmall };
    std::array<swell::analog::PushKey, 4> ratioKeys, meterKeys;
    swell::analog::BatToggle power;
    swell::analog::PilotLamp lamp { juce::Colour (0xffe8321e) };
    swell::analog::VuMeter meter;
    std::unique_ptr<juce::ParameterAttachment> ratioAttach, meterAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttach;
    int ratioIndex = 0, meterIndex = 0;
    swell::LicenceBadge badge { juce::Colour (0x8cece5d3), juce::Colour (0xffece5d3), juce::Justification::centred };
    swell::analog::PresetBar presets { proc, true };
    juce::VBlankAttachment vblank;
};
