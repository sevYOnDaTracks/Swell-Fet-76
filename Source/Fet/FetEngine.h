#pragma once
#include <juce_dsp/juce_dsp.h>

namespace swell
{
// Compresseur / limiteur FET, famille « 76 ».
//  - Seuil fixe : c'est le bouton Input qui pousse le signal dans la compression, Output rattrape le niveau.
//  - Attaque 20 µs à 800 µs, relâchement 50 ms à 1,1 s (7 = le plus rapide, comme sur le matériel),
//    relâchement en partie dépendant du programme.
//  - Ratios 4, 8, 12, 20 à genou doux, et « All » (toutes les touches enfoncées) : courbe qui dépasse
//    (au-delà du seuil la sortie baisse quand l'entrée monte), attaque plus molle, distorsion accrue.
//  - Couleur : distorsion paire du FET proportionnelle à la réduction de gain, étage de sortie classe A
//    asymétrique. Le tout tourne au rythme suréchantillonné, d'où un repliement négligeable.
class FetEngine
{
public:
    enum Ratio { r4, r8, r12, r20, rAll };

    void prepare (double sampleRate, int channels)
    {
        fs = sampleRate;
        numChannels = juce::jlimit (1, 2, channels);
        inGain.reset (fs, 0.02);
        outGain.reset (fs, 0.02);
        dcCoef = 1.0f - (float) (juce::MathConstants<double>::twoPi * 5.0 / fs);
        updateTimes();
    }

    void reset()
    {
        gr = 0;
        for (auto& s : dcX) s = 0;
        for (auto& s : dcY) s = 0;
        inGain.setCurrentAndTargetValue (inGain.getTargetValue());
        outGain.setCurrentAndTargetValue (outGain.getTargetValue());
    }

    void setParameters (float inputDb, float outputDb, float attackKnob, float releaseKnob, int ratioIndex)
    {
        inGain.setTargetValue (juce::Decibels::decibelsToGain (inputDb));
        outGain.setTargetValue (juce::Decibels::decibelsToGain (outputDb));
        if (! juce::exactlyEqual (attackKnob, attack) || ! juce::exactlyEqual (releaseKnob, release) || ratioIndex != ratio)
        {
            attack = attackKnob; release = releaseKnob; ratio = ratioIndex;
            updateTimes();
        }
    }

    float process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        const int n = (int) block.getNumSamples();
        const int chs = juce::jmin (numChannels, (int) block.getNumChannels());
        float* ch[2] { block.getChannelPointer (0), chs > 1 ? block.getChannelPointer (1) : nullptr };
        float maxGr = 0;

        for (int i = 0; i < n; ++i)
        {
            const float gi = inGain.getNextValue(), go = outGain.getNextValue();
            float x[2] { ch[0][i] * gi, ch[1] != nullptr ? ch[1][i] * gi : 0.0f };

            // Détection couplée stéréo (paire liée) sur la crête instantanée.
            const float peak = juce::jmax (std::abs (x[0]), std::abs (x[1]));
            const float levelDb = 20.0f * std::log10 (peak + 1.0e-9f);
            const float target = gainComputer (levelDb);

            // Lissage à deux branches dans le domaine des dB (attaque / relâchement indépendants).
            if (target > gr) gr = attCoef * gr + (1.0f - attCoef) * target;
            else
            {
                // Plus la compression est forte, plus le retour est lent (relâchement en deux temps).
                const float w = juce::jlimit (0.0f, 1.0f, gr / 24.0f);
                const float a = relCoefFast + (relCoefSlow - relCoefFast) * w;
                gr = a * gr + (1.0f - a) * target;
            }
            maxGr = juce::jmax (maxGr, gr);
            const float g = std::exp2 (-gr * 0.166096404f);   // 10^(-gr/20)
            const float fetAmount = (ratio == rAll ? 0.10f : 0.035f) + (ratio == rAll ? 0.6f : 0.22f) * (1.0f - g);

            for (int c = 0; c < chs; ++c)
            {
                float y = x[c] * g;
                y += fetAmount * y * y / (1.0f + std::abs (y));   // FET en résistance variable : harmoniques paires
                y *= go;
                y = classA (y);
                const float out = y - dcX[c] + dcCoef * dcY[c];   // blocage du continu (5 Hz)
                dcX[c] = y; dcY[c] = out;
                ch[c][i] = out;
            }
        }
        return maxGr;
    }

    static float attackSeconds (float knob)  { return 800.0e-6f * std::pow (20.0f / 800.0f, (knob - 1.0f) / 6.0f); }
    static float releaseSeconds (float knob) { return 1.1f * std::pow (0.05f / 1.1f, (knob - 1.0f) / 6.0f); }

private:
    float gainComputer (float levelDb) const noexcept
    {
        const float over = levelDb - threshold;
        float reduction;
        if (2.0f * over < -knee) reduction = 0.0f;
        else if (2.0f * std::abs (over) <= knee) reduction = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        else reduction = slope * over;
        return juce::jmax (0.0f, reduction);
    }

    static float classA (float y) noexcept
    {
        // Saturation douce asymétrique, pente unité autour de zéro.
        constexpr float C = 1.6f, b = 0.12f;
        const float t0 = std::tanh (b / C);
        const float slope0 = 1.0f - t0 * t0;
        return (C * std::tanh ((y + b) / C) - C * t0) / slope0;
    }

    void updateTimes()
    {
        static constexpr float ratios[] { 4.0f, 8.0f, 12.0f, 20.0f, 20.0f };
        static constexpr float knees[] { 8.0f, 6.0f, 5.0f, 3.0f, 14.0f };
        const int r = juce::jlimit (0, 4, ratio);
        knee = knees[r];
        // Toutes les touches : pente > 1 (la réduction dépasse l'excès), d'où la célèbre courbe en « S ».
        slope = (1.0f - 1.0f / ratios[r]) * (r == rAll ? 1.18f : 1.0f);
        threshold = r == rAll ? -16.0f : -14.0f;
        const float att = attackSeconds (attack) * (r == rAll ? 1.8f : 1.0f);
        const float rel = releaseSeconds (release) * (r == rAll ? 1.3f : 1.0f);
        attCoef = std::exp (-1.0f / (att * (float) fs));
        relCoefFast = std::exp (-1.0f / (rel * (float) fs));
        relCoefSlow = std::exp (-1.0f / (rel * 2.2f * (float) fs));
    }

    double fs = 44100.0;
    int numChannels = 2;
    float attack = 4, release = 5;
    int ratio = 0;
    float threshold = -14, knee = 8, slope = 0.75f;
    float attCoef = 0, relCoefFast = 0, relCoefSlow = 0;
    float gr = 0;
    float dcCoef = 0.999f;
    float dcX[2] {}, dcY[2] {};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inGain { 1.0f }, outGain { 1.0f };
};
} // namespace swell
