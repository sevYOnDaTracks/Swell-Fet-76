#include "FetEditor.h"

using namespace juce;
using namespace swell::analog;

namespace
{
constexpr int W = 1100, H = 330;
const Colour ink (0xffe4dccb);          // encre de sérigraphie, blanc cassé
const Colour panelTone (0xff1f1f21);
constexpr int keyX1 = 612, keyW = 44, keyH = 32;
constexpr int keyRows[] { 90, 130, 170, 210 };
const Rectangle<int> meterArea (742, 62, 222, 204);

VuMeter::Style meterStyle()
{
    VuMeter::Style s;
    s.face = Colour (0xfff3eddd);
    s.glow = Colour (0xfffff0c8);
    s.glowAmount = 0.28f;
    s.caption = "SWELL";
    return s;
}

// Façade : peinture noire satinée à grain fin, sur un alu dont le brossage transparaît à peine.
const Image& blackPanel()
{
    static const Image img = []
    {
        Image base = paintedPanel (W * 2, H * 2, Colour (0xff222224), 0.9f, 76);
        {
            Graphics g (base);
            g.setOpacity (0.28f);
            g.drawImageAt (brushedMetal (W * 2, H * 2, Colour (0xff2a2a2d), 0.10f, 1176), 0, 0);
        }
        addGrain (base, 0.035f, 9);
        return base;
    }();
    return img;
}

const std::vector<std::pair<Point<float>, float>> knobSpots { { { 178, 178 }, 59 }, { { 368, 178 }, 59 }, { { 488, 126 }, 31 },
                                                              { { 488, 246 }, 31 }, { { 1004, 112 }, 25 }, { { 990, 228 }, 12 } };
}

FetEditor::FetEditor (FetProcessor& p)
    : AnalogEditor (p, W, H), fet (p), meter (meterStyle()),
      vblank (this, [this] { updateMeter(); })
{
    panel.drawStatic = [this] (Graphics& g) { drawFacePlate (g); };

    attach (input, "input", 140, { 178, 178 });
    attach (output, "output", 140, { 368, 178 });
    attach (attack, "attack", 74, { 488, 126 });
    attach (release, "release", 74, { 488, 246 });
    attach (mix, "mix", 60, { 1004, 112 });

    panel.addAndMakeVisible (meter);
    meter.setBounds (meterArea);

    // Touches de ratio (20 en haut, 4 en bas). Maj + clic = toutes enfoncées.
    ratioAttach = std::make_unique<ParameterAttachment> (*p.apvts.getParameter ("ratio"),
                                                         [this] (float v) { ratioIndex = roundToInt (v); syncKeys(); });
    meterAttach = std::make_unique<ParameterAttachment> (*p.apvts.getParameter ("meter"),
                                                         [this] (float v) { meterIndex = roundToInt (v); syncKeys(); });
    for (int k = 0; k < 4; ++k)
    {
        auto& rk = ratioKeys[(size_t) k];
        panel.addAndMakeVisible (rk);
        rk.setBounds (keyX1, keyRows[k], keyW, keyH);
        rk.setTooltip (String::fromUTF8 ("Maj + clic : toutes les touches enfoncées (All)"));
        rk.onClick = [this, k]
        {
            const bool all = ModifierKeys::currentModifiers.isShiftDown();
            ratioAttach->setValueAsCompleteGesture (all ? 4.0f : (float) (3 - k));
        };
    }
    panel.addAndMakeVisible (meterModes);
    meterModes.setBounds (meterArea.getX() + 10, meterArea.getBottom() + 6, meterArea.getWidth() - 20, 18);
    meterModes.onPick = [this] (int k) { meterAttach->setValueAsCompleteGesture ((float) k); };
    ratioAttach->sendInitialUpdate();
    meterAttach->sendInitialUpdate();

    panel.addAndMakeVisible (power);
    power.setBounds (Rectangle<int> (54, 54).withCentre ({ 990, 228 }));
    powerAttach = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "power", power);

    panel.addAndMakeVisible (presets);
    presets.setBounds (560, 17, 232, 28);
    panel.addAndMakeVisible (badge);
    badge.setBounds (330, 302, 400, 18);

    restoreSize();
}

void FetEditor::syncKeys()
{
    for (int k = 0; k < 4; ++k)
        ratioKeys[(size_t) k].setToggleState (ratioIndex == 4 || ratioIndex == 3 - k, dontSendNotification);
    meterModes.active = meterIndex;
    meterModes.repaint();
}

void FetEditor::updateMeter()
{
    presets.refresh();
    const bool on = power.getToggleState();
    meter.setLit (on);
    if (! on || meterIndex == 3) { meter.setTargetDb (-100.0f); return; }
    if (meterIndex == 0) { meter.setTargetDb (-fet.meterGainReductionDb.load()); return; }
    // 0 VU = -18 dBFS RMS en position +4, -14 dBFS en +8 (calibration studio usuelle).
    const float rmsDb = Decibels::gainToDecibels (fet.meterOutputRms.load(), -100.0f);
    meter.setTargetDb (rmsDb + (meterIndex == 2 ? 18.0f : 14.0f));
}

namespace
{
const char* meterLabels[] { "GR", "+8", "+4", "OFF" };
}

void FetEditor::MeterModes::paint (Graphics& g)
{
    const float w = (float) getWidth() / 4.0f;
    for (int k = 0; k < 4; ++k)
        drawPrinted (g, meterLabels[k], { w * (float) k, 0, w, (float) getHeight() }, panelFont (9.5f, true),
                     ink.withAlpha (k == active ? 1.0f : 0.32f));
}

void FetEditor::MeterModes::mouseUp (const MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition())) return;
    const int k = jlimit (0, 3, e.x * 4 / jmax (1, getWidth()));
    if (onPick) onPick (k);
}

void FetEditor::drawFacePlate (Graphics& g)
{
    const auto full = Rectangle<float> (0, 0, (float) W, (float) H);
    g.drawImage (blackPanel(), full, RectanglePlacement::stretchToFit);
    drawPanelLight (g, full, 1.0f);

    // Usure d'abord : la sérigraphie et les pièces viennent par-dessus.
    WearSpec wear;
    wear.area = full.reduced (52, 0);
    wear.hotspots = knobSpots;
    wear.seed = 76;
    drawWear (g, wear);
    drawBevelEdge (g, full, 0, 1.0f);

    drawRackEar (g, { 0, 0, 52, (float) H }, Colour (0xff2a2a2d), true);
    drawRackEar (g, { (float) W - 52, 0, 52, (float) H }, Colour (0xff2a2a2d), true);

    // En-tête sérigraphié.
    drawPrinted (g, "S W E L L", { 72, 16, 200, 30 }, panelFont (23, true), ink, Justification::centredLeft);
    g.setColour (ink.withAlpha (0.6f));
    g.fillRect (72.0f, 47.0f, 150.0f, 1.0f);
    drawPrinted (g, "FET 76", { 230, 16, 120, 30 }, panelFont (23, false), ink, Justification::centredLeft);
    drawPrinted (g, "PEAK LIMITING AMPLIFIER", { 360, 22, 260, 20 }, panelFont (11, true), ink.withAlpha (0.8f), Justification::centredLeft);

    // Graduations des gros boutons.
    ScaleSpec in;
    in.centre = { 178, 178 }; in.radius = 64; in.tickLen = 7; in.labelRadius = 84;
    in.start = -2.36f; in.end = 2.36f; in.minorPerMajor = 2; in.fontSize = 11; in.ink = ink; in.panelTone = panelTone;
    in.labels = { "-20", "-10", "0", "10", "20", "30", "40" };
    drawKnobScale (g, in);
    ScaleSpec out = in;
    out.centre = { 368, 178 };
    out.labels = { "-30", "-20", "-10", "0", "+10", "+20" };
    drawKnobScale (g, out);
    drawPrinted (g, "INPUT", { 128, 284, 100, 20 }, panelFont (12.5f, true), ink);
    drawPrinted (g, "OUTPUT", { 318, 284, 100, 20 }, panelFont (12.5f, true), ink);

    ScaleSpec sm;
    sm.radius = 32; sm.tickLen = 5; sm.labelRadius = 42; sm.start = -2.36f; sm.end = 2.36f;
    sm.fontSize = 9.5f; sm.ink = ink; sm.panelTone = panelTone; sm.labels = { "1", "2", "3", "4", "5", "6", "7" };
    sm.centre = { 488, 126 }; drawKnobScale (g, sm);
    sm.centre = { 488, 246 }; drawKnobScale (g, sm);
    drawPrinted (g, "ATTACK", { 438, 58, 100, 14 }, panelFont (10, true), ink);
    drawPrinted (g, "RELEASE", { 438, 178, 100, 14 }, panelFont (10, true), ink);

    // Touches de ratio.
    drawPrinted (g, "RATIO", { (float) keyX1 - 30, 66, 106, 16 }, panelFont (10, true), ink);
    const char* ratioLabels[] { "20", "12", "8", "4" };
    for (int k = 0; k < 4; ++k)
        drawPrinted (g, ratioLabels[k], { (float) keyX1 - 34, (float) keyRows[k], 28, (float) keyH }, panelFont (11, true), ink, Justification::centredRight);

    // Découpe du vu-mètre : ombre du cadre sur la façade.
    Path cut;
    cut.addRoundedRectangle (meterArea.toFloat(), 10.0f);
    softShadow (g, cut, Colours::black.withAlpha (0.8f), 10.0f, { 2, 5 });

    drawKnobScale (g, [] { ScaleSpec s; s.centre = { 1004, 112 }; s.radius = 27; s.tickLen = 4; s.labelRadius = 38; s.panelTone = panelTone;
                           s.start = -2.36f; s.end = 2.36f; s.fontSize = 8; s.ink = ink; s.labels = { "DRY", "", "", "", "WET" }; return s; }());
    drawPrinted (g, "MIX", { 970, 62, 68, 14 }, panelFont (10, true), ink);
    drawPrinted (g, "POWER", { 960, 178, 60, 14 }, panelFont (9.5f, true), ink);
    drawPrinted (g, "ON", { 970, 194, 40, 10 }, panelFont (7.5f, true), ink.withAlpha (0.7f));

    drawPrinted (g, "SWELL-AUDIO  -  FRANCE", { 760, 304, 200, 14 }, panelFont (8, true), ink.withAlpha (0.42f), Justification::centredRight);
}
