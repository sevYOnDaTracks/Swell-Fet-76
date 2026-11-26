// Banc de mesure des presets d'usine de Swell FET 76 : chemin complet du plugin (processBlock, suréchantillonnage 2x,
// mix aligné), sur une vraie voix et des sources synthétiques (batterie, pièce, basse, 808, guitare).
// Pour chaque preset et chaque source : niveau RMS entrée / sortie, réduction de gain (moyenne, P95, max),
// facteur de crête avant / après, « queue » de batterie (pièce remontée), distorsion (THD sur sinus 100 Hz),
// et temps d'attaque / de relâchement mesurés sur un échelon de sinus.
//   PresetTestFET76 [dossier de sortie pour les WAV] [VOX.wav]
#include <juce_audio_formats/juce_audio_formats.h>
#include "Fet/FetProcessor.h"
#include <cstdio>
#include <map>

using namespace juce;
static constexpr double sr = 48000.0;
static constexpr int bs = 64;   // petits blocs : la réduction de gain lue au vu-mètre est quasi instantanée

static float rmsDb (const AudioBuffer<float>& b, int from = 0, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    double s = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = from; i < to; ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
    return (float) (10.0 * std::log10 (s / ((to - from) * b.getNumChannels()) + 1.0e-20));
}
static float peakDb (const AudioBuffer<float>& b)
{
    float m = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) m = jmax (m, b.getMagnitude (c, 0, b.getNumSamples()));
    return Decibels::gainToDecibels (m, -200.0f);
}

// --- Sources -----------------------------------------------------------------------------------------
static AudioBuffer<float> mono2 (const std::vector<float>& v)
{
    AudioBuffer<float> b (2, (int) v.size());
    for (int i = 0; i < (int) v.size(); ++i) { b.setSample (0, i, v[(size_t) i]); b.setSample (1, i, v[(size_t) i]); }
    return b;
}
static void normalisePeak (std::vector<float>& v, float db)
{
    float m = 0; for (auto x : v) m = jmax (m, std::abs (x));
    const float g = Decibels::decibelsToGain (db) / jmax (m, 1.0e-9f);
    for (auto& x : v) x *= g;
}

// Batterie 100 BPM : grosse caisse, caisse claire, charleston, vélocités variées. room = ajoute une queue de pièce.
static std::vector<float> drums (bool room, std::vector<int>* hits = nullptr)
{
    const int n = (int) (8.0 * sr);
    std::vector<float> v ((size_t) n, 0.0f);
    Random r (7);
    const double beat = 60.0 / 100.0;
    auto rise = [] (double s) { return s < 0.001 ? 0.5 - 0.5 * std::cos (3.14159265 * s / 0.001) : 1.0; };   // montée de 1 ms, comme un vrai coup capté
    auto add = [&] (int start, auto fn, double len)
    {
        for (int i = 0; i < (int) (len * sr) && start + i < n; ++i) v[(size_t) (start + i)] += fn (i / sr);
    };
    for (int b = 0; b < 13; ++b)
    {
        const int t = (int) (b * beat * sr);
        const float vel = b % 4 == 3 ? 0.7f : 1.0f;
        if (hits) hits->push_back (t);
        if (b % 2 == 0) // grosse caisse
            add (t, [&] (double s) { const double f = 50 + 90 * std::exp (-s * 30); return vel * (float) (std::sin (6.2831853 * f * s) * std::exp (-s * 9)); }, 0.5);
        else // caisse claire
            add (t, [&] (double s) { return vel * (float) (rise (s) * ((0.6 * (r.nextFloat() * 2 - 1) * std::exp (-s * 22)) + 0.5 * std::sin (6.2831853 * 190 * s) * std::exp (-s * 30))); }, 0.4);
        for (int h = 0; h < 2; ++h) // charleston en croches
            add (t + (int) (h * beat * 0.5 * sr), [&] (double s) { return 0.12f * (float) (rise (s) * (r.nextFloat() * 2 - 1) * std::exp (-s * 60)); }, 0.08);
    }
    if (room) // pièce : copies diffuses retardées, queue longue (~0,6 s), 10 dB sous l'attaque
    {
        std::vector<float> wet ((size_t) n, 0.0f);
        float lp = 0;
        for (int i = 0; i < n; ++i)
        {
            float acc = 0;
            for (int k = 1; k <= 24; ++k)
            {
                const int d = (int) (sr * (0.008 + 0.025 * k)) ;
                if (i - d >= 0) acc += v[(size_t) (i - d)] * (float) std::exp (-0.025 * k / 0.25) * ((k & 1) ? 1.0f : -1.0f);
            }
            lp += 0.3f * (acc - lp);
            wet[(size_t) i] = lp;
        }
        for (int i = 0; i < n; ++i) v[(size_t) i] = 0.55f * v[(size_t) i] + 0.6f * wet[(size_t) i];
    }
    normalisePeak (v, -6.0f);
    return v;
}

// Basse au doigt : notes en croches, vélocités ±6 dB, onde riche filtrée.
static std::vector<float> bass()
{
    const int n = (int) (8.0 * sr);
    std::vector<float> v ((size_t) n, 0.0f);
    const float notes[] { 41.2f, 41.2f, 55.0f, 49.0f, 41.2f, 61.7f, 55.0f, 36.7f };
    const float vels[] { 1.0f, 0.5f, 0.8f, 0.6f, 1.0f, 0.45f, 0.9f, 0.7f };
    const double step = 60.0 / 100.0 / 2.0;
    double ph = 0; float lp = 0, lp2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const int k = (int) (i / (step * sr));
        const double s = i / sr - k * step;
        const float f = notes[k % 8], vel = vels[k % 8];
        ph += f / sr; ph -= std::floor (ph);
        const float saw = (float) (2 * ph - 1);
        const float env = vel * (float) (std::exp (-s * 3) * (s < step * 0.9 ? 1.0 : 0.2) * (1 - std::exp (-s * 400)));
        lp += 0.05f * (saw * env - lp); lp2 += 0.08f * (lp - lp2);
        v[(size_t) i] = lp2;
    }
    normalisePeak (v, -8.0f);
    return v;
}

// 808 : sinus 45 Hz à longue décroissance avec clic, une note par mesure et demie.
static std::vector<float> eightOhEight()
{
    const int n = (int) (8.0 * sr);
    std::vector<float> v ((size_t) n, 0.0f);
    const double len = 1.2;
    for (int i = 0; i < n; ++i)
    {
        const int k = (int) (i / (len * sr));
        const double s = i / sr - k * len;
        const double f = 45 + 60 * std::exp (-s * 40);
        const float vel = (k % 2) ? 0.6f : 1.0f;
        v[(size_t) i] = vel * (float) (std::sin (6.2831853 * f * s) * std::exp (-s * 1.6));
    }
    normalisePeak (v, -6.0f);
    return v;
}

// Guitare / piano : accords pincés (Karplus-Strong), vélocités variées.
static std::vector<float> pluck (std::vector<int>* hits = nullptr)
{
    const int n = (int) (8.0 * sr);
    std::vector<float> v ((size_t) n, 0.0f);
    Random r (5);
    const float chord[] { 82.4f, 123.5f, 164.8f, 207.7f, 246.9f, 329.6f };
    for (int c = 0; c < 8; ++c)
    {
        const int t0 = (int) (c * 1.0 * sr);
        if (hits) hits->push_back (t0);
        const float vel = (c % 3 == 0) ? 1.0f : (c % 3 == 1 ? 0.4f : 0.65f);
        for (int s = 0; s < 6; ++s)
        {
            const int L = (int) (sr / chord[s]);
            std::vector<float> line ((size_t) L);
            for (auto& x : line) x = r.nextFloat() * 2 - 1;
            const int start = t0 + s * 480;
            for (int i = 0, p = 0; start + i < n && i < (int) (1.6 * sr); ++i, p = (p + 1) % L)
            {
                const float out = line[(size_t) p];
                line[(size_t) p] = 0.996f * 0.5f * (line[(size_t) p] + line[(size_t) ((p + 1) % L)]);
                v[(size_t) (start + i)] += vel * out * 0.3f;
            }
        }
    }
    normalisePeak (v, -6.0f);
    return v;
}

static AudioBuffer<float> loadWav (const File& f)
{
    AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return {};
    AudioBuffer<float> b (2, (int) rd->lengthInSamples);
    rd->read (&b, 0, b.getNumSamples(), 0, true, true);
    return b;
}
static void writeWav (const File& f, const AudioBuffer<float>& b)
{
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
    if (w) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// --- Rendu -------------------------------------------------------------------------------------------
struct Render { AudioBuffer<float> out; std::vector<float> gr; int latency = 0; };
using Values = std::vector<std::pair<const char*, float>>;
static const Values* candidate = nullptr;   // non nul : réglages candidats appliqués à la place du preset d'usine

static Render render (int preset, const AudioBuffer<float>& in)
{
    FetProcessor p;
    p.setPlayConfigDetails (2, 2, sr, bs);
    p.setCurrentProgram (preset);
    if (candidate != nullptr)
        for (auto& [id, v] : *candidate)
        {
            auto* prm = p.apvts.getParameter (id);
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
        }
    p.prepareToPlay (sr, bs);
    Render r;
    r.latency = p.getLatencySamples();
    const int n = in.getNumSamples();
    r.out.setSize (2, n);
    AudioBuffer<float> blk (2, bs);
    MidiBuffer midi;
    for (int s = 0; s < n + r.latency; s += bs)
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i) blk.setSample (c, i, s + i < n ? in.getSample (c, s + i) : 0.0f);
        p.processBlock (blk, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < bs; ++i)
            {
                const int d = s + i - r.latency;
                if (d >= 0 && d < n) r.out.setSample (c, d, blk.getSample (c, i));
            }
        r.gr.push_back (p.meterGainReductionDb.load());
    }
    return r;
}

struct Stats { float dRms, inRms, outRms, outPeak, grAvg, grP95, grMax, crestIn, crestOut, tailIn = 0, tailOut = 0; };

static Stats measure (int preset, const AudioBuffer<float>& in, const std::vector<int>* hits, const File& wavOut)
{
    auto r = render (preset, in);
    if (wavOut != File()) writeWav (wavOut, r.out);
    Stats s {};
    s.inRms = rmsDb (in); s.outRms = rmsDb (r.out); s.dRms = s.outRms - s.inRms;
    s.outPeak = peakDb (r.out);
    s.crestIn = peakDb (in) - s.inRms; s.crestOut = s.outPeak - s.outRms;
    // GR : seulement là où il y a du signal (moyenne sur les blocs actifs de l'entrée).
    std::vector<float> act;
    for (size_t k = 0; k < r.gr.size(); ++k)
    {
        const int a = (int) k * bs, b = jmin (a + bs, in.getNumSamples());
        if (a < b && rmsDb (in, a, b) > -50.0f) act.push_back (r.gr[k]);
    }
    if (! act.empty())
    {
        double sum = 0; for (auto g : act) sum += g; s.grAvg = (float) (sum / act.size());
        std::sort (act.begin(), act.end());
        s.grP95 = act[(size_t) (0.95 * (act.size() - 1))]; s.grMax = act.back();
    }
    if (hits) // queue = énergie 150-450 ms après le coup, relative aux 25 premières ms (dB)
    {
        double ti = 0, to = 0; int cnt = 0;
        for (int h : *hits)
        {
            const int a = h, b = h + (int) (0.025 * sr), c = h + (int) (0.15 * sr), d = h + (int) (0.45 * sr);
            if (d >= in.getNumSamples()) continue;
            ti += rmsDb (in, c, d) - rmsDb (in, a, b);
            to += rmsDb (r.out, c, d) - rmsDb (r.out, a, b);
            ++cnt;
        }
        s.tailIn = (float) (ti / cnt); s.tailOut = (float) (to / cnt);
    }
    return s;
}

// THD d'un sinus (Goertzel sur les harmoniques 2 à 10), mesuré après la mise en route.
static float thdPercent (int preset, float freq, float ampDb)
{
    const int n = (int) (1.5 * sr);
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = Decibels::decibelsToGain (ampDb) * (float) std::sin (6.2831853 * freq * i / sr);
    auto r = render (preset, mono2 (v));
    const int a = (int) (0.75 * sr), len = (int) (0.5 * sr);
    auto mag = [&] (double f)
    {
        double re = 0, im = 0;
        for (int i = 0; i < len; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (6.283185307 * i / len);
            re += w * r.out.getSample (0, a + i) * std::cos (6.283185307 * f * i / sr);
            im += w * r.out.getSample (0, a + i) * std::sin (6.283185307 * f * i / sr);
        }
        return std::sqrt (re * re + im * im);
    };
    const double fund = mag (freq);
    double h = 0;
    for (int k = 2; k <= 10; ++k) { const double m = mag (freq * k); h += m * m; }
    return (float) (100.0 * std::sqrt (h) / jmax (fund, 1.0e-12));
}

// Échelon de sinus 1 kHz : -40 dBFS -> +20 dB pendant 0,6 s -> retour. Attaque = temps pour atteindre 90 % de la
// réduction finale ; relâchement = temps pour revenir sous 10 % (mesurés sur l'enveloppe de sortie, au niveau d'entrée constant).
static void stepTimes (int preset, float /*unused*/, float& attackMs, float& releaseMs, float& grStep)
{
    float inputDb = 0;
    {
        FetProcessor p; p.setCurrentProgram (preset);
        if (candidate) for (auto& [id, v] : *candidate) if (String (id) == "input") p.apvts.getParameter (id)->setValueNotifyingHost (p.apvts.getParameter (id)->convertTo0to1 (v));
        inputDb = p.apvts.getRawParameterValue ("input")->load();
    }
    const float baseDb = -24.0f - inputDb;   // 10 dB sous le seuil, puis échelon à 10 dB au-dessus
    const int n = (int) (2.6 * sr), up = (int) (0.6 * sr), down = (int) (1.2 * sr);
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = Decibels::decibelsToGain (i >= up && i < down ? baseDb + 20.0f : baseDb) * (float) std::sin (6.2831853 * 1000.0 * i / sr);
    auto in = mono2 (v);
    auto r = render (preset, in);
    // Gain instantané par période (48 échantillons) : sortie / entrée.
    const int per = 48;
    auto gainAt = [&] (int i) { return rmsDb (r.out, i, i + per) - rmsDb (in, i, i + per); };
    const float gBefore = gainAt (up - 4 * per), gHigh = gainAt (down - 4 * per), gAfter = gainAt (n - 4 * per);
    grStep = (gBefore - gHigh) - 20.0f * 0.0f;
    // en entrée haute, la réduction totale = gBefore - gHigh (le premier palier peut déjà compresser)
    const float target = gBefore - 0.9f * (gBefore - gHigh);
    attackMs = -1;
    for (int i = up; i < down - per; i += 8) if (gainAt (i) <= target) { attackMs = (float) ((i - up) * 1000.0 / sr); break; }
    const float relTarget = gAfter - 0.1f * (gAfter - gHigh);
    releaseMs = -1;
    for (int i = down; i < n - per; i += 8) if (gainAt (i) >= relTarget) { releaseMs = (float) ((i - down) * 1000.0 / sr); break; }
}

// --- Cahier des charges des presets -----------------------------------------------------------------
// Chaque preset d'usine a une source cible, une réduction de gain moyenne visée et un caractère vérifiable.
enum class Kind { normal, punch, parallel, room, tame, peak, crush, sustain };
struct Spec { const char* category; const char* name; int ratio; float attack, release, mix; const char* source; float grTarget; Kind kind; };

static const std::vector<Spec> specs {
    { "Vocals",      "Vocal Smooth",          0, 3.0f, 5.0f, 100, "vox",    5.0f, Kind::tame },
    { "Vocals",      "Vocal Upfront",         1, 5.0f, 6.0f, 100, "vox",    8.0f, Kind::tame },
    { "Vocals",      "Vocal Aggressive",      3, 6.0f, 7.0f, 100, "vox",   12.0f, Kind::tame },
    { "Vocals",      "Vocal Parallel Grit",   4, 6.0f, 6.0f,  40, "vox",   20.0f, Kind::parallel },
    { "Vocals",      "Vocal Peak Catcher",    3, 7.0f, 5.0f, 100, "vox",    2.0f, Kind::peak },
    { "Drums",       "Kick Punch",            0, 1.0f, 6.0f, 100, "drums",  5.0f, Kind::punch },
    { "Drums",       "Snare Crack",           0, 2.0f, 7.0f, 100, "drums",  6.0f, Kind::punch },
    { "Drums",       "Drum Bus Punch",        0, 2.0f, 7.0f, 100, "drums",  4.0f, Kind::punch },
    { "Drums",       "Drum Bus Parallel",     4, 6.0f, 7.0f,  40, "drums", 20.0f, Kind::parallel },
    { "Drums",       "Room Smash",            4, 7.0f, 7.0f, 100, "room",  20.0f, Kind::room },
    { "Bass",        "Bass Even",             0, 3.0f, 3.0f, 100, "bass",   5.0f, Kind::tame },
    { "Bass",        "Bass Grip",             1, 5.0f, 4.0f, 100, "bass",   8.0f, Kind::tame },
    { "Bass",        "808 Control",           1, 3.0f, 2.0f, 100, "808",    5.0f, Kind::tame },
    { "Instruments", "Guitar Sustain",        1, 3.0f, 7.0f, 100, "gtr",   10.0f, Kind::sustain },
    { "Instruments", "Acoustic Guitar",       0, 2.0f, 4.0f, 100, "gtr",    4.0f, Kind::normal },
    { "Instruments", "Piano Gentle",          0, 2.0f, 3.0f, 100, "gtr",    3.0f, Kind::normal },
    { "Color",       "All Buttons Crush",     4, 6.0f, 7.0f, 100, "drums", 18.0f, Kind::crush },
};

struct Sources
{
    std::map<String, AudioBuffer<float>> buf;
    std::vector<int> drumHits, roomHits, gtrHits;
    const std::vector<int>* hitsFor (const String& k) const { return k == "drums" ? &drumHits : (k == "room" ? &roomHits : (k == "gtr" ? &gtrHits : nullptr)); }
};

static Values valuesOf (const Spec& s, float input, float output)
{
    Values v { { "input", input }, { "output", output }, { "attack", s.attack }, { "release", s.release }, { "ratio", (float) s.ratio } };
    if (s.mix < 100) v.push_back ({ "mix", s.mix });
    return v;
}

// Cherche l'entrée qui donne la réduction visée, puis la sortie qui rend le même niveau RMS (pas de 0,5 dB).
static Values tune (const Spec& s, const Sources& src)
{
    const auto& in = src.buf.at (s.source);
    const auto* hits = src.hitsFor (s.source);
    float lo = -20, hi = 40, output = 0;
    Values v;
    for (int it = 0; it < 14; ++it)
    {
        const float mid = 0.5f * (lo + hi);
        v = valuesOf (s, mid, 0); candidate = &v;
        (measure (0, in, hits, {}).grAvg < s.grTarget ? lo : hi) = mid;
    }
    const float input = std::round (lo * 2) / 2;
    for (int it = 0; it < 4; ++it)
    {
        v = valuesOf (s, input, output); candidate = &v;
        auto st = measure (0, in, hits, {});
        output -= jmax (st.dRms, st.outPeak + 1.0f);   // niveau égal, sans dépasser -1 dBFS en crête
    }
    output = std::floor (output * 2) / 2;
    candidate = nullptr;
    return valuesOf (s, input, output);
}

static bool checkPreset (const Spec& s, const Stats& st, float thd, String& why)
{
    bool ok = true;
    auto need = [&] (bool c, const String& what) { if (! c) { ok = false; why << what << "; "; } };
    // Même niveau RMS (±1,5 dB), sauf si les transitoires qui passent l'attaque feraient écrêter :
    // la sortie est alors calée sur une crête de -1 dBFS (au plus 4 dB sous le niveau d'entrée).
    const bool peakLimited = st.outPeak >= -1.6f && st.dRms < -1.5f && st.dRms >= -4.0f;
    need (std::abs (st.dRms) <= 1.5f || peakLimited, "niveau " + String (st.dRms, 1) + " dB (tolérance ±1,5)");
    need (st.outPeak <= -0.3f, "crête de sortie " + String (st.outPeak, 1) + " dBFS");
    need (st.grAvg >= 0.7f * s.grTarget && st.grAvg <= 1.3f * s.grTarget, "réduction moyenne " + String (st.grAvg, 1) + " dB (visée " + String (s.grTarget, 1) + ")");
    const float dCrest = st.crestOut - st.crestIn, tail = st.tailOut - st.tailIn;
    switch (s.kind)
    {
        case Kind::punch:    need (dCrest >= 0.0f, "attaque écrasée (crête " + String (dCrest, 1) + " dB)"); break;
        case Kind::parallel: need (String (s.source) == "vox" ? dCrest <= -1.5f : tail >= 2.0f, "parallèle sans effet (crête " + String (dCrest, 1) + ", queue " + String (tail, 1) + ")"); break;
        case Kind::room:     need (tail >= 6.0f, "pièce pas assez remontée (queue " + String (tail, 1) + " dB)"); break;
        case Kind::tame:     need (dCrest <= -1.5f, "dynamique pas tenue (crête " + String (dCrest, 1) + " dB)"); break;
        case Kind::peak:     need (st.grP95 >= 2.5f * st.grAvg && dCrest <= -1.5f, "ne travaille pas sur les crêtes (P95 " + String (st.grP95, 1) + ", crête " + String (dCrest, 1) + ")"); break;
        case Kind::crush:    need (thd >= 2.0f && st.grAvg >= 15.0f, "pas assez écrasé / coloré (THD " + String (thd, 2) + " %)"); break;
        case Kind::sustain:  need (tail >= 2.0f, "pas de sustain gagné (queue " + String (tail, 1) + " dB)"); break;
        case Kind::normal:   break;
    }
    return ok;
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    swell::Licence::unlockForTesting();
    StringArray args;
    for (int i = 1; i < argc; ++i) args.add (String::fromUTF8 (argv[i]));
    const bool tuneMode = args.contains ("--tune"); args.removeString ("--tune");
    const bool reportMode = args.contains ("--report"); args.removeString ("--report");
    File voxFile = File::getCurrentWorkingDirectory().getChildFile ("../Vocal-Match-Tests/VOX.wav");
    File outDir;
    for (auto& a : args) (a.endsWithIgnoreCase (".wav") ? voxFile : outDir) = File::getCurrentWorkingDirectory().getChildFile (a);
    if (outDir != File()) outDir.createDirectory();

    Sources src;
    src.buf["vox"] = loadWav (voxFile);
    src.buf["drums"] = mono2 (drums (false, &src.drumHits));
    src.buf["room"] = mono2 (drums (true, &src.roomHits));
    src.buf["bass"] = mono2 (bass());
    src.buf["808"] = mono2 (eightOhEight());
    src.buf["gtr"] = mono2 (pluck (&src.gtrHits));
    if (src.buf["vox"].getNumSamples() == 0) { std::printf ("VOX.wav introuvable : %s\n", voxFile.getFullPathName().toRawUTF8()); return 1; }
    if (outDir != File()) for (auto& [k, b] : src.buf) writeWav (outDir.getChildFile ("_source-" + k + ".wav"), b);

    std::printf ("Sources : ");
    for (auto& [k, b] : src.buf) std::printf ("%s %.1f dB RMS / %.1f dB crête   ", k.toRawUTF8(), rmsDb (b), peakDb (b));
    std::printf ("\n\n");

    if (tuneMode)
    {
        // Calcule Input / Output de chaque preset du cahier des charges et imprime la liste à coller dans FetProcessor.cpp.
        for (auto& s : specs)
        {
            auto v = tune (s, src);
            candidate = &v;
            auto st = measure (0, src.buf.at (s.source), src.hitsFor (s.source),
                               outDir != File() ? outDir.getChildFile (String (s.name).replaceCharacter (' ', '_') + "-" + s.source + ".wav") : File());
            const float thd = thdPercent (0, 100, -12);
            float att, rel, grs; stepTimes (0, -40.0f, att, rel, grs);
            candidate = nullptr;
            String why; const bool ok = checkPreset (s, st, thd, why);
            String line; line << "        { \"" << s.category << "\", \"" << s.name << "\", {";
            for (auto& [id, x] : v) line << " { \"" << id << "\", " << String (x, 1).replace (".0", "") << " },";
            line = line.dropLastCharacters (1) + " } },";
            std::printf ("%s\n    // %s : niveau %+.1f dB, GR moy %.1f / P95 %.1f dB, crête %+.1f dB, queue %+.1f dB, crête sortie %.1f dBFS, THD %.2f %%, attaque %.2f ms, relâchement %.0f ms %s%s\n",
                         line.toRawUTF8(), s.source, st.dRms, st.grAvg, st.grP95, st.crestOut - st.crestIn, st.tailOut - st.tailIn, st.outPeak, thd, att, rel,
                         ok ? "" : "  ** ", why.toRawUTF8());
        }
        return 0;
    }

    FetProcessor names;
    const int np = names.getNumPrograms();
    if (reportMode)
    {
        std::printf ("%-22s %-6s %7s %7s %6s %6s %6s %7s %7s %7s\n", "preset", "source", "dRMS", "pkOut", "GRavg", "GRp95", "GRmax", "crestIn", "crestOut", "queue+");
        for (int i = 0; i < np; ++i)
        {
            const auto name = names.getProgramName (i);
            for (auto& [k, b] : src.buf)
            {
                const File wav = outDir != File() && (k == "vox" || k == "drums" || k == "room" || k == "bass")
                                   ? outDir.getChildFile (String (i).paddedLeft ('0', 2) + "-" + name.replaceCharacter (' ', '_') + "-" + k + ".wav") : File();
                auto s = measure (i, b, src.hitsFor (k), wav);
                std::printf ("%-22s %-6s %+7.1f %7.1f %6.1f %6.1f %6.1f %7.1f %7.1f %+7.1f\n", name.toRawUTF8(), k.toRawUTF8(),
                             s.dRms, s.outPeak, s.grAvg, s.grP95, s.grMax, s.crestIn, s.crestOut, src.hitsFor (k) ? s.tailOut - s.tailIn : 0.0f);
            }
            float att, rel, grs;
            stepTimes (i, -40.0f, att, rel, grs);
            std::printf ("%-22s THD 100 Hz @-12 dBFS %.2f %%, @-24 %.2f %% | échelon seuil-10 -> seuil+10 dB : GR %.1f dB, attaque %.2f ms, relâchement %.0f ms\n\n",
                         name.toRawUTF8(), thdPercent (i, 100, -12), thdPercent (i, 100, -24), grs, att, rel);
        }
        return 0;
    }

    // Vérification : chaque preset d'usine (hors Default) tient son cahier des charges sur sa source cible.
    int failures = 0, checked = 0;
    for (int i = 0; i < np; ++i)
    {
        const auto name = names.getProgramName (i);
        auto it = std::find_if (specs.begin(), specs.end(), [&] (const Spec& s) { return name == s.name; });
        if (it == specs.end()) { if (i > 0) { std::printf ("  [FAIL] %s : pas de cahier des charges\n", name.toRawUTF8()); ++failures; } continue; }
        auto st = measure (i, src.buf.at (it->source), src.hitsFor (it->source),
                           outDir != File() ? outDir.getChildFile (String (i).paddedLeft ('0', 2) + "-" + name.replaceCharacter (' ', '_') + ".wav") : File());
        const float thd = thdPercent (i, 100, -12);
        String why; const bool ok = checkPreset (*it, st, thd, why);
        std::printf ("  [%s] %-20s (%s) niveau %+.1f dB, GR %.1f dB, crête %+.1f dB, queue %+.1f dB, THD %.2f %% %s\n", ok ? " ok " : "FAIL",
                     name.toRawUTF8(), it->source, st.dRms, st.grAvg, st.crestOut - st.crestIn, st.tailOut - st.tailIn, thd, why.toRawUTF8());
        failures += ok ? 0 : 1; ++checked;
    }
    std::printf (failures == 0 ? "%d presets conformes.\n" : "%d presets vérifiés, %d hors cahier des charges.\n", checked, failures);
    return failures == 0 ? 0 : 1;
}
