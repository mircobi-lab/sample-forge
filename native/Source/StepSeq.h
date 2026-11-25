// Sample Forge · MBStudio91 — sequencer a 16 passi per battuta (fino a 4 battute), come nella versione web:
// un bit per passo e per pad, swing sui passi pari, metronomo, mute per banco, REC quantizzato.
// I dati sono atomici: l'interfaccia li scrive, il thread audio li legge e, in REC, ci scrive i colpi.
#pragma once
#include "Sampler.h"
#include <juce_data_structures/juce_data_structures.h>
#include <cmath>
#include <limits>

namespace sf
{
constexpr int stepsPerBar = 16, maxBars = 4, maxSteps = stepsPerBar * maxBars;

struct SeqData
{
    std::array<std::atomic<uint64_t>, numPads> steps {};      // bit s = passo s
    std::array<std::atomic<bool>, numBanks> mute {};
    std::atomic<int> bars { 4 }, quant { 1 };                  // quant: 1 = 1/16, 2 = 1/8, 4 = 1/4
    std::atomic<float> bpm { 120.f }, swing { 0.f };           // swing 0..100 come nella versione web
    std::atomic<bool> playing { false }, rec { false }, metro { true };

    int total() const noexcept { return juce::jlimit (1, maxBars, bars.load()) * stepsPerBar; }
    bool get (int pad, int step) const noexcept { return (steps[(size_t) pad].load() >> step) & 1u; }
    void set (int pad, int step, bool on) noexcept
    {
        const uint64_t bit = (uint64_t) 1 << step;
        if (on) steps[(size_t) pad].fetch_or (bit); else steps[(size_t) pad].fetch_and (~bit);
    }
    void clearBank (int bank) noexcept { for (int p = 0; p < padsPerBank; ++p) steps[(size_t) (bank * padsPerBank + p)] = 0; }
    bool empty() const noexcept { for (auto& s : steps) if (s.load() != 0) return false; return true; }

    // ritardo del passo pari: swing/100 x un terzo di passo (formula della versione web)
    static double swingDelaySteps (int step, float swingAmount) noexcept { return (step & 1) ? juce::jlimit (0.f, 100.f, swingAmount) / 100.0 * .33 : 0.0; }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("Seq");
        t.setProperty ("bars", bars.load(), nullptr); t.setProperty ("quant", quant.load(), nullptr);
        t.setProperty ("bpm", bpm.load(), nullptr); t.setProperty ("swing", swing.load(), nullptr); t.setProperty ("metro", metro.load(), nullptr);
        juce::StringArray s, m;
        for (auto& x : steps) s.add (juce::String::toHexString ((juce::int64) x.load()));
        for (auto& x : mute) m.add (x.load() ? "1" : "0");
        t.setProperty ("steps", s.joinIntoString (" "), nullptr);
        t.setProperty ("mute", m.joinIntoString (""), nullptr);
        return t;
    }
    void fromValueTree (const juce::ValueTree& t)
    {
        if (! t.hasType ("Seq")) return;
        bars = juce::jlimit (1, maxBars, (int) t.getProperty ("bars", 4));
        const int q = (int) t.getProperty ("quant", 1); quant = (q == 2 || q == 4) ? q : 1;
        bpm = juce::jlimit (40.f, 300.f, (float) t.getProperty ("bpm", 120.0));
        swing = juce::jlimit (0.f, 100.f, (float) t.getProperty ("swing", 0.0));
        metro = (bool) t.getProperty ("metro", true);
        auto s = juce::StringArray::fromTokens (t.getProperty ("steps").toString(), " ", "");
        for (int i = 0; i < numPads; ++i) steps[(size_t) i] = i < s.size() ? (uint64_t) s[i].getHexValue64() : 0;
        const auto m = t.getProperty ("mute").toString();
        for (int b = 0; b < numBanks; ++b) mute[(size_t) b] = m.length() > b && m[b] == '1';
    }
};

/** Motore del sequencer sul thread audio: produce i colpi dentro il blocco e registra quelli suonati a mano. */
class SeqEngine
{
public:
    struct Hit { int offset, pad; };          // pad -1 = clic del metronomo (forte se all'inizio della battuta: pad -2)
    std::atomic<int> stepUi { -1 };

    void prepare (double sampleRate) { sr = sampleRate; stop(); }
    void stop() { running = false; stepUi = -1; for (auto& s : skip) s = -1; }

    // host: posizione in quarti se la DAW suona, altrimenti tempo interno. Restituisce i colpi del blocco.
    template <size_t N>
    int process (SeqData& d, int n, bool hostPlaying, double hostBpm, double hostPpq, std::array<Hit, N>& out)
    {
        int count = 0;
        if (! d.playing.load()) { if (running) stop(); return 0; }
        const double bpm = hostPlaying && hostBpm > 0 ? hostBpm : (double) juce::jlimit (40.f, 300.f, d.bpm.load());
        const double stepsPerSample = bpm / 60.0 * 4.0 / sr;
        const int total = d.total();
        double p0;
        if (hostPlaying) p0 = hostPpq * 4.0;
        else
        {
            if (! running || wasHost || std::abs (bpm - freeBpm) > 1e-9) { freeBase = running && ! wasHost ? curPos : 0; freeSamples = 0; freeBpm = bpm; }
            p0 = freeBase + (double) freeSamples * stepsPerSample;
        }
        if (! running || (hostPlaying && std::abs (p0 - lastEnd) > 4 * stepsPerSample)) lastK = std::numeric_limits<long long>::min();
        const double p1 = p0 + n * stepsPerSample;
        blockP0 = p0; blockSps = stepsPerSample;
        const float sw = d.swing.load();
        constexpr double eps = 1e-9;
        for (long long k = (long long) std::floor (p0) - 1; k <= (long long) std::floor (p1) + 1 && count < (int) N; ++k)
        {
            if (k <= lastK) continue;
            const int st = (int) (((k % total) + total) % total);
            const double t = (double) k + SeqData::swingDelaySteps (st, sw);
            if (t < p0 - eps) continue;
            if (t >= p1 - eps) break;
            const int off = juce::jlimit (0, n - 1, (int) std::llround ((t - p0) / stepsPerSample));
            lastK = k;
            curStep = st;
            if (d.metro.load() && st % 4 == 0 && count < (int) N) out[(size_t) count++] = { off, st % stepsPerBar == 0 ? -2 : -1 };
            for (int pad = 0; pad < numPads && count < (int) N; ++pad)
            {
                if (d.mute[(size_t) (pad / padsPerBank)].load() || ! d.get (pad, st)) continue;
                if (skip[(size_t) pad] == st) { skip[(size_t) pad] = -1; continue; }   // colpo appena registrato: già suonato
                out[(size_t) count++] = { off, pad };
            }
        }
        curPos = p1;
        lastEnd = p1;
        if (! hostPlaying) freeSamples += n;
        running = true;
        wasHost = hostPlaying;
        stepUi = curStep;
        return count;
    }

    /** Colpo suonato a mano durante REC: va sul passo quantizzato più vicino. offset = campione nel blocco corrente
        (chiamare dopo process() dello stesso blocco). */
    void record (SeqData& d, int pad, int offset)
    {
        if (! running || ! d.rec.load() || pad < 0 || pad >= numPads) return;
        const double pos = blockP0 + offset * blockSps;
        const int total = d.total(), q = juce::jlimit (1, 4, d.quant.load());
        const long long target = (long long) std::llround (pos / q) * q;
        const int st = (int) (((target % total) + total) % total);
        d.set (pad, st, true);
        if (target > lastK) skip[(size_t) pad] = st;     // passo non ancora suonato: il colpo a mano basta
    }

private:
    double sr = 44100, freeBase = 0, freeBpm = 0, curPos = 0, lastEnd = -1, blockP0 = 0, blockSps = 0;
    long long freeSamples = 0, lastK = std::numeric_limits<long long>::min();
    bool running = false, wasHost = false;
    int curStep = -1;
    std::array<int, numPads> skip {};
};

/** Metronomo: clic sinusoidale con decadimento esponenziale (1760 Hz forte, 1100 Hz debole, 40 ms). */
struct Click
{
    double phase = 0, inc = 0; float amp = 0, decay = .999f;
    void trigger (bool accent, double sr) { phase = 0; inc = (accent ? 1760.0 : 1100.0) / sr; amp = accent ? .25f : .14f; decay = (float) std::exp (std::log (.001 / amp) / (.04 * sr)); }
    void render (juce::AudioBuffer<float>& b, int start, int n)
    {
        if (amp < 1e-4f) return;
        for (int i = 0; i < n; ++i)
        {
            const float v = amp * (float) std::sin (phase * juce::MathConstants<double>::twoPi);
            b.addSample (0, start + i, v); b.addSample (1, start + i, v);
            phase += inc; amp *= decay;
            if (amp < 1e-4f) { amp = 0; break; }
        }
    }
};

/** Loop della sequenza reso offline: N battute esatte, la coda oltre la fine si somma all'inizio (loop continuo),
    picco riportato sotto 0 dBFS. Stessa logica di renderPattern() nella versione web. */
inline juce::AudioBuffer<float> renderLoop (const SeqData& d, const PadSet& set, const std::array<BankSettings, numBanks>& bankSettings)
{
    Sampler s;
    for (int b = 0; b < numBanks; ++b)
    {
        auto& dst = s.settings[(size_t) b]; const auto& src = bankSettings[(size_t) b];
        dst.vol = src.vol.load(); dst.pitch = src.pitch.load(); dst.attack = src.attack.load(); dst.decay = src.decay.load();
        dst.sustain = src.sustain.load(); dst.release = src.release.load(); dst.choke = src.choke.load();
        // nel loop un pad LATCH suona una volta, come nella versione web (niente loop infinito)
        dst.mode = src.mode.load() == (int) PlayMode::latch ? (int) PlayMode::oneShot : src.mode.load();
    }
    const double sr = set.sampleRate;
    s.prepare (sr);
    const int total = d.total();
    const double stepSec = 60.0 / juce::jlimit (40.0, 300.0, (double) d.bpm.load()) / 4.0;
    const int loopLen = (int) std::llround (total * stepSec * sr);
    const int len = loopLen + (int) (2.0 * sr);
    juce::AudioBuffer<float> out (2, len);
    out.clear();
    struct T { int at, pad; };
    std::vector<T> hits;
    for (int st = 0; st < total; ++st)
    {
        const int at = (int) std::llround ((st + SeqData::swingDelaySteps (st, d.swing.load())) * stepSec * sr);
        for (int pad = 0; pad < numPads; ++pad)
            if (! d.mute[(size_t) (pad / padsPerBank)].load() && d.get (pad, st) && set.has (pad)) hits.push_back ({ at, pad });
    }
    int pos = 0;
    for (auto& h : hits)
    {
        if (h.at > pos) { s.render (&set, out, pos, h.at - pos); pos = h.at; }
        s.noteOn (&set, h.pad, 1.f);
    }
    if (pos < len) s.render (&set, out, pos, len - pos);
    juce::AudioBuffer<float> loop (2, std::max (1, loopLen));
    loop.clear();
    for (int c = 0; c < 2; ++c)
    {
        const float* src = out.getReadPointer (c); float* dst = loop.getWritePointer (c);
        for (int i = 0; i < len; ++i) dst[i % loopLen] += src[i];
    }
    const float pk = loop.getMagnitude (0, loopLen);
    if (pk > .999f) loop.applyGain (.98f / pk);
    return loop;
}
}  // namespace sf
