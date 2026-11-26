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
    std::array<swell::analog::PushKey, 4> ratioKeys;

    // Choix de la mesure du vu-mètre : quatre mots sérigraphiés sous le cadran, l'actif est à pleine encre.
    struct MeterModes : juce::Component
    {
        int active = 0;
        std::function<void (int)> onPick;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
    } meterModes;
    juce::TooltipWindow tooltips { this, 700 };
    swell::analog::BatToggle power;
    swell::analog::VuMeter meter;
    std::unique_ptr<juce::ParameterAttachment> ratioAttach, meterAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttach;
    int ratioIndex = 0, meterIndex = 0;
    swell::LicenceBadge badge { juce::Colour (0x8cece5d3), juce::Colour (0xffece5d3), juce::Justification::centred };
    swell::analog::PresetBar presets { proc, true };
    juce::VBlankAttachment vblank;
};
