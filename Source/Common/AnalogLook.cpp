#include "AnalogLook.h"
#include <map>

namespace swell::analog
{
using namespace juce;

Font panelFont (float height, bool bold)
{
   #if JUCE_MAC
    // Grotesque industrielle, comme la sérigraphie des appareils de studio des années 70.
    return Font (FontOptions ("Helvetica Neue", height, bold ? Font::bold : Font::plain));
   #else
    return Font (FontOptions ("Arial", height, bold ? Font::bold : Font::plain));
   #endif
}

Font condensedFont (float height)
{
   #if JUCE_MAC
    return Font (FontOptions ("Avenir Next Condensed", height, Font::bold));
   #else
    return Font (FontOptions ("Arial Narrow", height, Font::bold));
   #endif
}

float physicalScale (Graphics& g)
{
    return jlimit (0.5f, 6.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
}

//==============================================================================
static void boxBlurRows (std::vector<float>& buf, int w, int h, int radius)
{
    std::vector<float> row ((size_t) w);
    const float norm = 1.0f / (float) (2 * radius + 1);
    for (int y = 0; y < h; ++y)
    {
        float* p = buf.data() + (size_t) y * (size_t) w;
        float sum = 0;
        for (int k = -radius; k <= radius; ++k) sum += p[(k + w) % w];
        for (int x = 0; x < w; ++x)
        {
            row[(size_t) x] = sum * norm;
            sum += p[(x + radius + 1) % w] - p[(x - radius + w) % w];
        }
        std::copy (row.begin(), row.end(), p);
    }
}

static void normalise (std::vector<float>& buf)
{
    double mean = 0, sq = 0;
    for (auto v : buf) { mean += v; sq += (double) v * v; }
    mean /= (double) buf.size();
    const auto sd = std::sqrt (jmax (1.0e-12, sq / (double) buf.size() - mean * mean));
    for (auto& v : buf) v = (float) (((double) v - mean) / sd);
}

static void writePixels (Image& img, const std::vector<float>& lum, Colour base, float strength,
                         std::function<float (float, float)> shading)
{
    const int w = img.getWidth(), h = img.getHeight();
    Image::BitmapData data (img, Image::BitmapData::writeOnly);
    const float r0 = base.getFloatRed(), g0 = base.getFloatGreen(), b0 = base.getFloatBlue();
    for (int y = 0; y < h; ++y)
    {
        const float fy = (float) y / (float) h;
        for (int x = 0; x < w; ++x)
        {
            const float k = (1.0f + strength * lum[(size_t) y * (size_t) w + (size_t) x]) * shading ((float) x / (float) w, fy);
            data.setPixelColour (x, y, Colour::fromFloatRGBA (jlimit (0.0f, 1.0f, r0 * k),
                                                              jlimit (0.0f, 1.0f, g0 * k),
                                                              jlimit (0.0f, 1.0f, b0 * k), 1.0f));
        }
    }
}

Image brushedMetal (int w, int h, Colour base, float strength, uint32_t seed)
{
    Rng r (seed);
    std::vector<float> streaks ((size_t) w * (size_t) h);
    for (int y = 0; y < h; ++y)
    {
        const float rowTone = r.bipolar() * 0.6f;   // chaque passe de brosse a sa propre teinte
        for (int x = 0; x < w; ++x)
            streaks[(size_t) y * (size_t) w + (size_t) x] = rowTone + r.bipolar();
    }
    const int radius = jmax (2, w / 70);
    for (int pass = 0; pass < 3; ++pass)
        boxBlurRows (streaks, w, h, radius);
    normalise (streaks);
    for (auto& v : streaks) v = v * 0.85f + r.bipolar() * 0.35f;   // micro-rayures

    Image img (Image::RGB, w, h, false);
    writePixels (img, streaks, base, strength, [] (float, float fy)
    {
        // Reflet anisotrope typique du métal brossé : une bande claire horizontale.
        return 1.0f + 0.10f * std::exp (-std::pow ((fy - 0.28f) / 0.16f, 2.0f)) - 0.07f * fy;
    });
    return img;
}

static float valueNoise (const std::vector<float>& grid, int gw, int gh, float x, float y)
{
    const int x0 = (int) std::floor (x), y0 = (int) std::floor (y);
    const float fx = x - (float) x0, fy = y - (float) y0;
    auto at = [&] (int ix, int iy) { return grid[(size_t) (((iy % gh) + gh) % gh) * (size_t) gw + (size_t) (((ix % gw) + gw) % gw)]; };
    const float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
    const float a = at (x0, y0) + (at (x0 + 1, y0) - at (x0, y0)) * sx;
    const float b = at (x0, y0 + 1) + (at (x0 + 1, y0 + 1) - at (x0, y0 + 1)) * sx;
    return a + (b - a) * sy;
}

Image paintedPanel (int w, int h, Colour base, float mottling, uint32_t seed)
{
    Rng r (seed);
    const int gw = 64, gh = 64;
    std::vector<float> g1 ((size_t) (gw * gh)), g2 ((size_t) (gw * gh));
    for (auto& v : g1) v = r.bipolar();
    for (auto& v : g2) v = r.bipolar();

    std::vector<float> lum ((size_t) w * (size_t) h);
    const float cell1 = (float) w / 28.0f, cell2 = (float) w / 110.0f;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const float m = valueNoise (g1, gw, gh, (float) x / cell1, (float) y / cell1) * 0.65f
                          + valueNoise (g2, gw, gh, (float) x / cell2, (float) y / cell2) * 0.35f;
            lum[(size_t) y * (size_t) w + (size_t) x] = m * mottling + r.bipolar() * 0.35f;
        }

    Image img (Image::RGB, w, h, false);
    writePixels (img, lum, base, 0.05f, [] (float, float fy) { return 1.03f - 0.06f * fy; });
    return img;
}

void addGrain (Image& img, float amount, uint32_t seed)
{
    Rng r (seed);
    Image::BitmapData data (img, Image::BitmapData::readWrite);
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
        {
            auto c = data.getPixelColour (x, y);
            if (c.getAlpha() == 0) continue;
            const float k = 1.0f + r.bipolar() * amount;
            data.setPixelColour (x, y, Colour::fromFloatRGBA (jlimit (0.0f, 1.0f, c.getFloatRed() * k),
                                                              jlimit (0.0f, 1.0f, c.getFloatGreen() * k),
                                                              jlimit (0.0f, 1.0f, c.getFloatBlue() * k), c.getFloatAlpha()));
        }
}

//==============================================================================
void softShadow (Graphics& g, const Path& p, Colour c, float radius, Point<float> offset)
{
    // DropShadow travaille en pixels entiers : on le rend dans le repère physique pour rester net en Retina.
    const auto s = physicalScale (g);
    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::scale (1.0f / s));
    Path scaled (p);
    scaled.applyTransform (AffineTransform::scale (s));
    DropShadow (c, jmax (1, roundToInt (radius * s)), (offset * s).roundToInt()).drawForPath (g, scaled);
}

void drawScrew (Graphics& g, Point<float> c, float r, float angle, bool phillips)
{
    Path head;
    head.addEllipse (c.x - r, c.y - r, 2 * r, 2 * r);
    softShadow (g, head, Colours::black.withAlpha (0.55f), r * 0.45f, { r * 0.12f, r * 0.22f });

    // Fraisure : petit anneau sombre où la vis entre dans le panneau.
    g.setColour (Colours::black.withAlpha (0.35f));
    g.drawEllipse (c.x - r * 1.06f, c.y - r * 1.06f, r * 2.12f, r * 2.12f, r * 0.12f);

    ColourGradient metal (Colour (0xffeeeeea), c.x - r * 0.6f, c.y - r * 0.7f, Colour (0xff55575a), c.x + r * 0.7f, c.y + r * 0.8f, true);
    metal.addColour (0.45, Colour (0xffb4b6b8));
    g.setGradientFill (metal);
    g.fillPath (head);

    // Usinage concentrique.
    for (int i = 1; i < 6; ++i)
    {
        const float rr = r * (float) i / 6.0f;
        g.setColour (Colours::white.withAlpha (i % 2 ? 0.06f : 0.0f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, r * 0.05f);
    }

    auto slot = [&] (float a)
    {
        Path s;
        s.addRoundedRectangle (-r * 0.78f, -r * 0.13f, r * 1.56f, r * 0.26f, r * 0.08f);
        s.applyTransform (AffineTransform::rotation (a).translated (c));
        g.setColour (Colour (0xff1c1d1f));
        g.fillPath (s);
        // Arête éclairée côté opposé à la lumière.
        Path lip (s);
        lip.applyTransform (AffineTransform::translation (r * 0.04f, r * 0.07f));
        g.setColour (Colours::white.withAlpha (0.28f));
        g.strokePath (lip, PathStrokeType (r * 0.05f));
    };
    slot (angle);
    if (phillips) slot (angle + MathConstants<float>::halfPi);

    ColourGradient spec (Colours::white.withAlpha (0.55f), c.x - r * 0.35f, c.y - r * 0.45f,
                         Colours::white.withAlpha (0.0f), c.x - r * 0.35f + r * 0.55f, c.y - r * 0.45f, true);
    g.setGradientFill (spec);
    g.fillPath (head);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.strokePath (head, PathStrokeType (r * 0.06f));
}

void drawRackEar (Graphics& g, Rectangle<float> ear, Colour, bool dark)
{
    const float sw = ear.getWidth() * 0.34f, sh = ear.getHeight() * 0.16f;
    for (float fy : { 0.2f, 0.8f })
    {
        const Rectangle<float> slot (ear.getCentreX() - sw * 0.5f, ear.getY() + ear.getHeight() * fy - sh * 0.5f, sw, sh);
        Path hole;
        hole.addRoundedRectangle (slot, sw * 0.5f);
        // Le trou laisse voir le rail du rack, dans l'ombre.
        g.setColour (Colour (0xff0b0b0c));
        g.fillPath (hole);
        {
            Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (hole);
            ColourGradient rail (Colour (0xff3a3b3e), slot.getX(), slot.getY(), Colour (0xff111112), slot.getRight(), slot.getBottom(), false);
            g.setGradientFill (rail);
            g.fillRect (slot.reduced (sw * 0.15f, 0));
            // Ombre portée de l'épaisseur du panneau.
            g.setColour (Colours::black.withAlpha (0.7f));
            g.fillRect (slot.withHeight (sh * 0.22f));
        }
        g.setColour (dark ? Colours::white.withAlpha (0.10f) : Colours::white.withAlpha (0.45f));
        g.drawLine (slot.getX() + sw * 0.25f, slot.getBottom() + 0.5f, slot.getRight() - sw * 0.25f, slot.getBottom() + 0.5f, 0.8f);

        // Vis de rack avec rondelle.
        const auto c = slot.getCentre().translated (0, sh * 0.08f);
        const float wr = sw * 0.62f;
        Path washer;
        washer.addEllipse (c.x - wr, c.y - wr, wr * 2, wr * 2);
        softShadow (g, washer, Colours::black.withAlpha (0.6f), wr * 0.35f, { wr * 0.1f, wr * 0.25f });
        ColourGradient wg (Colour (0xffd8d8d4), c.x - wr, c.y - wr, Colour (0xff6d6e70), c.x + wr, c.y + wr, false);
        g.setGradientFill (wg);
        g.fillPath (washer);
        drawScrew (g, c, wr * 0.74f, 0.4f + fy, true);
    }
}

// Tuile de manques d'encre : taches éparses, de la taille d'un grain de sérigraphie à une petite éraflure.
static const Image& inkWearTile()
{
    static const Image tile = []
    {
        Image img (Image::ARGB, 192, 192, true);
        Graphics g (img);
        Rng r (4242);
        for (int i = 0; i < 900; ++i)
        {
            const float x = r.next() * 192, y = r.next() * 192, s = 0.4f + r.next() * r.next() * 2.2f;
            g.setColour (Colours::white.withAlpha (0.25f + 0.75f * r.next()));
            g.fillEllipse (x, y, s, s * (0.5f + r.next()));
        }
        for (int i = 0; i < 40; ++i)   // éraflures courtes
        {
            const float x = r.next() * 192, y = r.next() * 192, a = r.next() * 6.28f, l = 2 + r.next() * 7;
            g.setColour (Colours::white.withAlpha (0.4f + 0.5f * r.next()));
            g.drawLine (x, y, x + std::cos (a) * l, y + std::sin (a) * l, 0.5f + r.next() * 0.6f);
        }
        return img;
    }();
    return tile;
}

void drawInk (Graphics& g, const Path& printed, Colour ink, Colour panelTone, uint32_t seed)
{
    Rng r (seed);
    // Bavure : un liseré très léger autour des lettres, l'encre a un peu coulé dans la peinture.
    g.setColour (ink.withMultipliedAlpha (0.22f));
    g.strokePath (printed, PathStrokeType (0.45f));
    g.setColour (ink.withMultipliedAlpha (0.93f));
    g.fillPath (printed);

    Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (printed);
    // Encre inégale : grain clair / sombre.
    g.setTiledImageFill (grainTile(), (int) (r.next() * 256), (int) (r.next() * 256), 0.10f);
    g.fillPath (printed);
    // Manques : la couleur de la façade réapparaît (tuile décalée pour que chaque texte s'use autrement).
    static std::map<uint32, Image> tintedByTone;   // thread graphique seulement
    auto& tinted = tintedByTone[panelTone.getARGB()];
    if (tinted.isNull())
    {
        tinted = inkWearTile().createCopy();
        Image::BitmapData d (tinted, Image::BitmapData::readWrite);
        for (int y = 0; y < tinted.getHeight(); ++y)
            for (int x = 0; x < tinted.getWidth(); ++x)
                d.setPixelColour (x, y, panelTone.withAlpha (d.getPixelColour (x, y).getFloatAlpha()));
    }
    g.setTiledImageFill (tinted, (int) (r.next() * 192), (int) (r.next() * 192), 0.55f);
    g.fillPath (printed);
}

void drawPrinted (Graphics& g, const String& text, Rectangle<float> area, const Font& font, Colour ink, Justification j)
{
    if (text.isEmpty()) return;
    GlyphArrangement ga;
    ga.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), j, 1, 1.0f);
    Path p;
    ga.createPath (p);
    drawInk (g, p, ink, Colour (0xff202022), (uint32_t) text.hashCode() ^ (uint32_t) (area.getX() * 13 + area.getY()));
}

//==============================================================================
void drawDust (Graphics& g, Rectangle<float> a, int count, float amount, uint32_t seed)
{
    Rng r (seed);
    for (int i = 0; i < count; ++i)
    {
        const float x = a.getX() + r.next() * a.getWidth(), y = a.getY() + r.next() * a.getHeight();
        const float s = 0.35f + r.next() * r.next() * 1.4f;
        g.setColour (Colour (0xffe8e2d4).withAlpha (amount * (0.05f + 0.22f * r.next())));
        g.fillEllipse (x, y, s, s);
    }
}

void drawWear (Graphics& g, const WearSpec& w)
{
    Rng r (w.seed);
    const auto a = w.area;
    const float k = w.amount;

    // 1. Zones lustrées : là où l'on tourne les boutons, la peinture satinée est polie par les doigts.
    for (auto& [c, rad] : w.hotspots)
    {
        const float rr = rad * 1.9f;
        ColourGradient sheen (Colours::white.withAlpha (0.035f * k), c.x - rad * 0.2f, c.y + rad * 0.15f,
                              Colours::transparentWhite, c.x - rad * 0.2f + rr, c.y + rad * 0.15f, true);
        g.setGradientFill (sheen);
        g.fillEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2);
    }

    // 2. Rayures circulaires : la jupe du bouton et les ongles tournent autour de l'axe.
    for (auto& [c, rad] : w.hotspots)
    {
        const int n = (int) (6 + rad * 0.15f);
        for (int i = 0; i < n; ++i)
        {
            const float rr = rad * (1.03f + 0.45f * r.next());
            const float a0 = r.next() * MathConstants<float>::twoPi, len = 0.08f + r.next() * r.next() * 0.7f;
            Path arc;
            arc.addCentredArc (c.x, c.y, rr, rr, 0, a0, a0 + len, true);
            const bool light = r.next() < 0.75f;
            g.setColour ((light ? Colours::white : Colours::black).withAlpha (k * (light ? 0.02f + 0.05f * r.next() : 0.12f * r.next())));
            g.strokePath (arc, PathStrokeType (0.35f + 0.5f * r.next()));
        }
    }

    // 3. Rayures fines éparses (outils, câbles, autres appareils posés dessus).
    for (int i = 0; i < (int) (90 * k); ++i)
    {
        const float x = a.getX() + r.next() * a.getWidth(), y = a.getY() + r.next() * a.getHeight();
        const float ang = (r.next() < 0.6f ? (r.bipolar() * 0.35f) : r.next() * MathConstants<float>::pi);
        const float len = 3.0f + 28.0f * r.next() * r.next();
        Path sc;
        sc.startNewSubPath (x, y);
        sc.quadraticTo (x + std::cos (ang) * len * 0.5f + r.bipolar(), y + std::sin (ang) * len * 0.5f + r.bipolar(),
                        x + std::cos (ang) * len, y + std::sin (ang) * len);
        g.setColour (Colours::white.withAlpha (k * (0.02f + 0.06f * r.next())));
        g.strokePath (sc, PathStrokeType (0.3f + 0.4f * r.next()));
    }

    // 4. Éclats de peinture sur les arêtes haute et basse : le métal nu apparaît.
    for (int i = 0; i < (int) (34 * k); ++i)
    {
        const bool top = r.next() < 0.45f;
        const float x = a.getX() + r.next() * a.getWidth();
        const float y = top ? a.getY() + 0.6f + r.next() * r.next() * 5.0f : a.getBottom() - 0.6f - r.next() * r.next() * 5.0f;
        const float s = 0.5f + r.next() * r.next() * r.next() * 3.5f;
        Path chip;
        const int pts = 5 + (int) (r.next() * 3);
        for (int p = 0; p < pts; ++p)
        {
            const float pa = MathConstants<float>::twoPi * (float) p / (float) pts, pr = s * (0.5f + 0.5f * r.next());
            const Point<float> q (x + std::cos (pa) * pr * 1.4f, y + std::sin (pa) * pr * 0.8f);
            if (p == 0) chip.startNewSubPath (q); else chip.lineTo (q);
        }
        chip.closeSubPath();
        g.setColour (w.metal.withAlpha (0.15f + 0.35f * r.next()));
        g.fillPath (chip);
        g.setColour (Colours::black.withAlpha (0.25f));
        g.strokePath (chip, PathStrokeType (0.3f));
    }

    // 5. Poussière et quelques peluches.
    drawDust (g, a, (int) (420 * k), k, w.seed * 3 + 1);
    for (int i = 0; i < (int) (5 * k); ++i)
    {
        const float x = a.getX() + r.next() * a.getWidth(), y = a.getY() + r.next() * a.getHeight();
        Path fib;
        fib.startNewSubPath (x, y);
        fib.cubicTo (x + r.bipolar() * 6, y + r.bipolar() * 6, x + r.bipolar() * 8, y + r.bipolar() * 8, x + r.bipolar() * 10, y + r.bipolar() * 10);
        g.setColour (Colour (0xffd8d2c4).withAlpha (0.10f + 0.10f * r.next()));
        g.strokePath (fib, PathStrokeType (0.35f));
    }
}

void drawPanelLight (Graphics& g, Rectangle<float> r, float strength)
{
    ColourGradient v (Colours::white.withAlpha (0.10f * strength), r.getX(), r.getY(),
                      Colours::black.withAlpha (0.22f * strength), r.getX(), r.getBottom(), false);
    v.addColour (0.35, Colours::transparentWhite);
    g.setGradientFill (v);
    g.fillRect (r);

    ColourGradient h (Colours::black.withAlpha (0.16f * strength), r.getX(), r.getY(),
                      Colours::black.withAlpha (0.20f * strength), r.getRight(), r.getY(), false);
    h.addColour (0.12, Colours::transparentBlack);
    h.addColour (0.88, Colours::transparentBlack);
    g.setGradientFill (h);
    g.fillRect (r);
}

void drawBevelEdge (Graphics& g, Rectangle<float> r, float corner, float strength)
{
    Path top, bottom;
    top.startNewSubPath (r.getX() + corner, r.getY() + 0.75f);
    top.lineTo (r.getRight() - corner, r.getY() + 0.75f);
    bottom.startNewSubPath (r.getX() + corner, r.getBottom() - 0.75f);
    bottom.lineTo (r.getRight() - corner, r.getBottom() - 0.75f);
    g.setColour (Colours::white.withAlpha (0.35f * strength));
    g.strokePath (top, PathStrokeType (1.2f));
    g.setColour (Colours::black.withAlpha (0.6f * strength));
    g.strokePath (bottom, PathStrokeType (1.5f));
}

void drawKnobScale (Graphics& g, const ScaleSpec& s)
{
    // Repères et chiffres forment une seule sérigraphie, usée d'un bloc.
    const int majors = jmax (2, s.labels.size());
    const int steps = (majors - 1) * s.minorPerMajor;
    Path printed;
    for (int i = 0; i <= steps; ++i)
    {
        const float a = s.start + (s.end - s.start) * (float) i / (float) steps;
        const bool major = i % s.minorPerMajor == 0;
        const float len = major ? s.tickLen : s.tickLen * 0.55f;
        Path tick;
        tick.startNewSubPath (s.centre.getPointOnCircumference (s.radius, a));
        tick.lineTo (s.centre.getPointOnCircumference (s.radius + len, a));
        PathStrokeType (major ? 1.5f : 0.9f, PathStrokeType::mitered, PathStrokeType::butt).createStrokedPath (tick, tick);
        printed.addPath (tick);
    }
    const auto font = panelFont (s.fontSize, true);
    for (int i = 0; i < s.labels.size(); ++i)
    {
        if (s.labels[i].isEmpty()) continue;
        const float a = s.start + (s.end - s.start) * (float) i / (float) (majors - 1);
        const auto box = Rectangle<float> (40, s.fontSize + 4).withCentre (s.centre.getPointOnCircumference (s.labelRadius, a));
        GlyphArrangement ga;
        ga.addFittedText (font, s.labels[i], box.getX(), box.getY(), box.getWidth(), box.getHeight(), Justification::centred, 1, 1.0f);
        Path glyphs;
        ga.createPath (glyphs);
        printed.addPath (glyphs);
    }
    drawInk (g, printed, s.ink, s.panelTone, s.seed ^ (uint32_t) (s.centre.x * 7 + s.centre.y));
}
} // namespace swell::analog
