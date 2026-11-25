// Sample Forge · MBStudio91 — brano di prova sintetico, stesso arrangiamento della versione web:
// 112 BPM, La minore, Am-F-C-G per 16 battute; accordi larghi ai lati, charleston, dalla battuta 5 cassa,
// rullante e basso, battute 5-8 e 13-16 una "voce" a formanti al centro.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <random>

namespace sf
{
inline juce::AudioBuffer<float> makeDemoSong (double sr = 44100.0)
{
    const double bpm = 112, beat = 60.0 / bpm;
    const int bars = 16;
    const int len = (int) std::ceil ((bars * 4 * beat + 1.5) * sr);
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    std::mt19937 rng (20261124u);
    std::uniform_real_distribution<float> uni (-1.f, 1.f);
    const double twoPi = juce::MathConstants<double>::twoPi;
    auto hz = [] (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); };
    auto addPan = [&] (int i, float v, float pan)
    {
        if (i < 0 || i >= len) return;
        const float a = (pan + 1.f) * juce::MathConstants<float>::pi * .25f;   // equal power, come StereoPanner
        out.addSample (0, i, v * std::cos (a) * .55f);
        out.addSample (1, i, v * std::sin (a) * .55f);
    };
    auto saw = [] (double ph) { return (float) (2.0 * (ph - std::floor (ph + .5))); };

    auto kick = [&] (double t)
    {
        const int s0 = (int) (t * sr), n = (int) (.4 * sr);
        double ph = 0;
        for (int i = 0; i < n; ++i)
        {
            const double tt = i / sr;
            const double f = tt < .12 ? 130.0 * std::pow (42.0 / 130.0, tt / .12) : 42.0;
            const double g = tt < .38 ? 1.1 * std::pow (.001 / 1.1, tt / .38) : 0.0;
            ph += f / sr;
            addPan (s0 + i, (float) (std::sin (ph * twoPi) * g), 0.f);
        }
    };
    auto snare = [&] (double t)
    {
        const int s0 = (int) (t * sr);
        juce::IIRFilter bp; bp.setCoefficients (juce::IIRCoefficients::makeBandPass (sr, 1900.0, .7));
        double ph = 0;
        for (int i = 0; i < (int) (.22 * sr); ++i)
        {
            const double tt = i / sr;
            float v = bp.processSingleSampleRaw (uni (rng)) * (float) (tt < .2 ? .9 * std::pow (.001 / .9, tt / .2) : 0.0);
            if (tt < .13) { ph += 190.0 / sr; v += (float) (std::sin (ph * twoPi) * (tt < .12 ? .5 * std::pow (.001 / .5, tt / .12) : 0.0)); }
            addPan (s0 + i, v, 0.f);
        }
    };
    auto hat = [&] (double t, bool open)
    {
        const int s0 = (int) (t * sr);
        juce::IIRFilter hp; hp.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, 7000.0));
        const double g0 = open ? .32 : .26, dec = open ? .28 : .045;
        for (int i = 0; i < (int) (.3 * sr); ++i)
        {
            const double tt = i / sr;
            addPan (s0 + i, hp.processSingleSampleRaw (uni (rng)) * (float) (tt < dec ? g0 * std::pow (.001 / g0, tt / dec) : 0.0), .15f);
        }
    };
    // nota a dente di sega filtrata: attacco 10 ms, discesa al 70%, rilascio rapido (come tone() nella versione web)
    auto tone = [&] (double t, double d, double m, float pan, double gain, double cut, double detuneCents)
    {
        const int s0 = (int) (t * sr), n = (int) ((d + .3) * sr);
        juce::IIRFilter lp; lp.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, cut, .7071));
        const double f = hz (m) * std::pow (2.0, detuneCents / 1200.0);
        double ph = 0, g = 0;
        const double c1 = std::exp (-1.0 / (.2 * sr)), c2 = std::exp (-1.0 / (.04 * sr));
        for (int i = 0; i < n; ++i)
        {
            const double tt = i / sr;
            if (tt < .01) g = gain * tt / .01;
            else if (tt < .02) g = gain;
            else if (tt < d) g = gain * .7 + (g - gain * .7) * c1;
            else g *= c2;
            ph += f / sr;
            addPan (s0 + i, lp.processSingleSampleRaw (saw (ph)) * (float) g, pan);
        }
    };
    auto voice = [&] (double t, double d, double m)
    {
        const int s0 = (int) (t * sr), n = (int) ((d + .05) * sr);
        const double f0 = hz (m);
        juce::IIRFilter fm[3];
        const double fq[3] = { 800, 1150, 2900 }, q[3] = { 8, 9, 10 }, a[3] = { 1, .6, .35 };
        for (int k = 0; k < 3; ++k) fm[k].setCoefficients (juce::IIRCoefficients::makeBandPass (sr, fq[k], q[k]));
        double ph = 0, lph = 0;
        for (int i = 0; i < n; ++i)
        {
            const double tt = i / sr;
            lph += 5.5 / sr;
            ph += (f0 + std::sin (lph * twoPi) * f0 * .012) / sr;
            const float x = saw (ph);
            float y = 0;
            for (int k = 0; k < 3; ++k) y += fm[k].processSingleSampleRaw (x) * (float) (a[k] * 3.0);
            double g = tt < .06 ? .5 * tt / .06 : (tt < d - .08 ? .5 : (tt < d ? .5 * (d - tt) / .08 : 0.0));
            addPan (s0 + i, y * (float) g, 0.f);
        }
    };

    const int prog[4][3] = { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };
    const int roots[4] = { 33, 29, 36, 31 };
    struct Mel { int m; double b, d; };
    const Mel melody[] = { { 69, 0, 1 }, { 72, 1, 1 }, { 71, 2, .5 }, { 69, 2.5, 1.5 }, { 65, 4, 1 }, { 69, 5, 1 }, { 72, 6, 2 },
                           { 67, 8, 1 }, { 64, 9, 1 }, { 67, 10, 1 }, { 72, 11, 1 }, { 71, 12, 2 }, { 67, 14, 1.5 } };
    for (int bar = 0; bar < bars; ++bar)
    {
        const double t0 = bar * 4 * beat;
        const int c = bar % 4;
        const bool full = bar >= 4;
        for (int m : prog[c]) { tone (t0, 4 * beat - .05, m + 12, -.9f, .07, 2400, 7); tone (t0 + .012, 4 * beat - .05, m + 12, .9f, .07, 2400, -7); }
        for (int e = 0; e < 8; ++e)
        {
            const double t = t0 + e * beat / 2;
            hat (t, e == 7);
            if (full) tone (t, beat / 2 - .04, roots[c] + (e == 6 ? 12 : 0), 0.f, .32, 420, 0);
        }
        if (full) { kick (t0); kick (t0 + 2 * beat); kick (t0 + 2.5 * beat); snare (t0 + beat); snare (t0 + 3 * beat); }
        if ((bar >= 4 && bar < 8) || bar >= 12)
        {
            const int half = (bar % 4) < 2 ? 0 : 8;
            for (auto& mm : melody)
                if (mm.b >= half && mm.b < half + 8 && (int) std::floor ((mm.b - half) / 4) == (bar % 2))
                    voice (t0 + (mm.b - half - (bar % 2) * 4) * beat, mm.d * beat, mm.m);
        }
    }
    const float pk = out.getMagnitude (0, len);
    if (pk > .98f) out.applyGain (.98f / pk);
    return out;
}
}  // namespace sf
