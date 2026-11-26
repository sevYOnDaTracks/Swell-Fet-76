#include "PresetBar.h"

namespace swell::analog
{
using namespace juce;

PresetBar::PresetBar (AnalogProcessorBase& p, bool onDarkPanel) : proc (p), dark (onDarkPanel)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    shown = proc.getPresetName();
}

void PresetBar::refresh()
{
    const auto name = proc.getPresetName();
    if (name != shown) { shown = name; repaint(); }
}

int PresetBar::zoneAt (Point<int> p) const
{
    const int arrow = getHeight();
    if (p.x < arrow) return 0;
    if (p.x > getWidth() - arrow) return 2;
    return 1;
}

void PresetBar::mouseMove (const MouseEvent& e) { setHover (zoneAt (e.getPosition())); }

void PresetBar::mouseUp (const MouseEvent& e)
{
    if (! contains (e.getPosition())) return;
    switch (zoneAt (e.getPosition()))
    {
        case 0: step (-1); break;
        case 2: step (1); break;
        default: showMenu(); break;
    }
}

void PresetBar::step (int direction)
{
    const int numFactory = (int) proc.factoryPresets().size();
    const auto user = proc.userPresets();
    const int total = numFactory + user.size();
    if (total == 0) return;

    int current = proc.getCurrentFactoryIndex();
    if (current < 0)
        for (int i = 0; i < user.size(); ++i)
            if (user[i].getFileNameWithoutExtension() == proc.getPresetName()) current = numFactory + i;
    const int next = ((current < 0 ? 0 : current + direction) % total + total) % total;
    if (next < numFactory) proc.loadFactoryPreset (next);
    else proc.loadUserPreset (user[next - numFactory]);
    refresh();
}

void PresetBar::showMenu()
{
    PopupMenu menu;
    const auto& factory = proc.factoryPresets();
    String category;
    for (size_t i = 0; i < factory.size(); ++i)
    {
        if (category != factory[i].category)
        {
            category = factory[i].category;
            menu.addSectionHeader (category);
        }
        menu.addItem (PopupMenu::Item (factory[i].name).setID (1 + (int) i)
                                                       .setTicked ((int) i == proc.getCurrentFactoryIndex()));
    }
    const auto user = proc.userPresets();
    menu.addSectionHeader ("Mes presets");
    if (user.isEmpty())
        menu.addItem (PopupMenu::Item ("(aucun pour l'instant)").setEnabled (false));
    for (int i = 0; i < user.size(); ++i)
        menu.addItem (PopupMenu::Item (user[i].getFileNameWithoutExtension()).setID (1000 + i)
                                   .setTicked (proc.getCurrentFactoryIndex() < 0 && user[i].getFileNameWithoutExtension() == proc.getPresetName()));
    menu.addSeparator();
    menu.addItem (PopupMenu::Item (String::fromUTF8 ("Enregistrer le preset\xe2\x80\xa6")).setID (900));
    menu.addItem (PopupMenu::Item ("Ouvrir le dossier des presets").setID (901));

    SafePointer<PresetBar> safe (this);
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()),
                        [safe, user] (int id)
                        {
                            if (safe == nullptr || id == 0) return;
                            auto& self = *safe;
                            if (id >= 1000) self.proc.loadUserPreset (user[id - 1000]);
                            else if (id == 900) self.saveAs();
                            else if (id == 901)
                            {
                                auto folder = self.proc.userPresetFolder();
                                folder.createDirectory();
                                folder.startAsProcess();
                            }
                            else self.proc.loadFactoryPreset (id - 1);
                            self.refresh();
                        });
}

void PresetBar::saveAs()
{
    auto folder = proc.userPresetFolder();
    folder.createDirectory();
    chooser = std::make_unique<FileChooser> ("Enregistrer le preset", folder.getChildFile (proc.getPresetName() + AnalogProcessorBase::presetExtension),
                                             String ("*") + AnalogProcessorBase::presetExtension);
    SafePointer<PresetBar> safe (this);
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (safe == nullptr || file == File()) return;
                              safe->proc.saveUserPreset (file.withFileExtension (AnalogProcessorBase::presetExtension));
                              safe->refresh();
                          });
}

void PresetBar::paint (Graphics& g)
{
    // Ruban Dymo collé sur la façade : plastique noir brillant, lettres blanches embossées,
    // un peu de travers comme sur une vraie étiquette faite à la pince.
    const auto b = getLocalBounds().toFloat();
    const float h = b.getHeight();
    const auto tape = b.reduced (2.0f, h * 0.12f);
    Path strip;
    strip.addRoundedRectangle (tape, 1.5f);
    strip.applyTransform (AffineTransform::rotation (-0.006f, tape.getCentreX(), tape.getCentreY()));
    softShadow (g, strip, Colours::black.withAlpha (0.7f), 2.5f, { 0.4f, 1.2f });
    g.setGradientFill (ColourGradient (Colour (0xff2a2a2e), 0, tape.getY(), Colour (0xff0b0b0d), 0, tape.getBottom(), false));
    g.fillPath (strip);
    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (strip);
        // Fines stries longitudinales du plastique et reflet brillant en haut.
        for (float y = tape.getY() + 1.0f; y < tape.getBottom(); y += 1.6f)
        {
            g.setColour (Colours::white.withAlpha (0.025f));
            g.drawHorizontalLine ((int) y, tape.getX(), tape.getRight());
        }
        g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.16f), 0, tape.getY(), Colours::transparentWhite, 0, tape.getY() + tape.getHeight() * 0.45f, false));
        g.fillRect (tape);
        g.setTiledImageFill (grainTile(), 0, 0, 0.06f);
        g.fillRect (tape);
    }
    g.setColour (Colours::black.withAlpha (0.8f));
    g.strokePath (strip, PathStrokeType (0.6f));

    // Lettres embossées : le plastique étiré blanchit, chaque lettre est frappée un peu de travers.
    auto emboss = [&] (const String& text, Rectangle<float> area, Justification j, float alpha, uint32_t seed)
    {
        GlyphArrangement ga;
        ga.addLineOfText (condensedFont (h * 0.50f).withExtraKerningFactor (0.08f), text, 0, 0);
        const auto box = ga.getBoundingBox (0, -1, true);
        const auto placed = box.withPosition (j.appliedToRectangle (box.toNearestInt(), area.toNearestInt()).getPosition().toFloat());
        Rng r (seed);
        for (int i = 0; i < ga.getNumGlyphs(); ++i)
        {
            auto& gl = ga.getGlyph (i);
            if (gl.isWhitespace()) continue;
            Path p;
            gl.createPath (p);
            const float dx = placed.getX() - box.getX(), dy = placed.getY() - box.getY() + r.bipolar() * 0.45f;
            p.applyTransform (AffineTransform::rotation (r.bipolar() * 0.025f, gl.getLeft() + gl.getRight() * 0.0f, gl.getBaselineY())
                                  .translated (dx, dy));
            g.setColour (Colours::black.withAlpha (0.7f * alpha));
            g.fillPath (p, AffineTransform::translation (0.5f, 0.8f));
            g.setColour (Colour (0xfff2f0ea).withAlpha (alpha * (0.82f + 0.12f * r.next())));
            g.fillPath (p);
            g.setColour (Colours::white.withAlpha (0.35f * alpha));
            g.strokePath (p, PathStrokeType (0.3f), AffineTransform::translation (-0.2f, -0.3f));
        }
    };
    const float a = h;
    emboss (String::fromUTF8 ("\xe2\x97\x80"), tape.withWidth (a), Justification::centred, hover == 0 ? 1.0f : 0.6f, 1);
    emboss (String::fromUTF8 ("\xe2\x96\xb6"), tape.withTrimmedLeft (tape.getWidth() - a), Justification::centred, hover == 2 ? 1.0f : 0.6f, 2);
    emboss (shown.toUpperCase(), tape.reduced (a, 0), Justification::centred, hover == 1 ? 1.0f : 0.92f, (uint32_t) shown.hashCode());
}
} // namespace swell::analog
