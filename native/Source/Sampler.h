// Sample Forge · MBStudio91 — campionatore a 5 banchi x 8 pad (thread audio, senza allocazioni)
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>

namespace sf
{
constexpr int numBanks = 5, padsPerBank = 8, numPads = numBanks * padsPerBank;
constexpr int firstMidiNote = 36;  // C1: banco 1 pad A ... banco 5 pad K = 75

enum class PlayMode : int { oneShot = 0, gate = 1, latch = 2 };

struct BankSettings
{
    std::atomic<float> vol { 1.f }, pitch { 0.f }, attack { 0.f }, decay { 0.3f }, sustain { 1.f }, release { 0.01f };
    std::atomic<int> mode { (int) PlayMode::oneShot };
    std::atomic<bool> choke { false };
};

/** Campioni pronti da suonare: costruito sul thread dei messaggi, letto dal thread audio. */
struct PadSet
{
    double sampleRate = 44100;
    std::array<juce::AudioBuffer<float>, numPads> buffers;   // 2 canali, vuoto = pad senza campione
    std::array<bool, numPads> loopable {};
    bool has (int i) const { return buffers[(size_t) i].getNumSamples() > 0; }
};

class Sampler
{
public:
    Sampler()
    {
        for (int b = 0; b < numBanks; ++b) applyDefaults (b);
        for (auto& p : playhead) p.store (-1.f);
        for (auto& l : latched) l.store (false);
    }

    void applyDefaults (int bank)
    {
        auto& s = settings[(size_t) bank];
        struct D { float vol, a, d, s, r; PlayMode m; bool choke; };
        static const D defs[numBanks] = { { 1.f, 0.f, 0.3f, 1.f, 0.01f, PlayMode::oneShot, false },
                                          { 1.f, 0.f, 0.3f, 1.f, 0.03f, PlayMode::oneShot, true },
                                          { .9f, 0.f, 0.3f, 1.f, 0.02f, PlayMode::latch, false },
                                          { .9f, .005f, 0.4f, 1.f, 0.15f, PlayMode::gate, true },
                                          { 1.f, .002f, 0.4f, 1.f, 0.05f, PlayMode::gate, true } };
        const auto& d = defs[bank];
        s.vol = d.vol; s.pitch = 0; s.attack = d.a; s.decay = d.d; s.sustain = d.s; s.release = d.r; s.mode = (int) d.m; s.choke = d.choke;
    }

    void prepare (double sr) { deviceRate = sr; for (auto& v : voices) v.active = false; }

    /** Chiamato dal thread audio con il set corrente (o nullptr). */
    void noteOn (const PadSet* set, int pad, float velocity = 1.f)
    {
        if (set == nullptr || pad < 0 || pad >= numPads || ! set->has (pad)) return;
        const int bank = pad / padsPerBank;
        const auto& s = settings[(size_t) bank];
        const auto mode = (PlayMode) s.mode.load();
        if (mode == PlayMode::latch && latched[(size_t) pad].load())
        {
            fadeOut (pad, 0.03f);
            latched[(size_t) pad] = false;
            return;
        }
        fadeOut (pad, 0.006f);                                   // ripremere un pad lo fa ripartire
        if (s.choke.load())
            for (int i = bank * padsPerBank; i < (bank + 1) * padsPerBank; ++i) if (i != pad) fadeOut (i, 0.012f);
        Voice* v = freeVoice();
        v->active = true; v->pad = pad; v->bank = bank; v->pos = 0; v->velocity = velocity;
        v->loop = mode == PlayMode::latch; v->stage = Stage::attack; v->level = 0; v->releaseStep = 0; v->forcedFade = false;
        latched[(size_t) pad] = mode == PlayMode::latch;
    }

    void noteOff (int pad)
    {
        if (pad < 0 || pad >= numPads) return;
        const auto& s = settings[(size_t) (pad / padsPerBank)];
        if ((PlayMode) s.mode.load() != PlayMode::gate) return;
        for (auto& v : voices)
            if (v.active && v.pad == pad && v.stage != Stage::release) startRelease (v, std::max (0.005f, s.release.load()));
    }

    void panic()
    {
        for (auto& v : voices) v.active = false;
        for (auto& l : latched) l = false;
        for (auto& p : playhead) p = -1.f;
    }

    void render (const PadSet* set, juce::AudioBuffer<float>& out, int start, int num)
    {
        std::array<float, numPads> ph; ph.fill (-1.f);
        if (set != nullptr)
            for (auto& v : voices)
            {
                if (! v.active) continue;
                const auto& buf = set->buffers[(size_t) v.pad];
                const int len = buf.getNumSamples();
                if (len < 2) { v.active = false; continue; }
                const auto& s = settings[(size_t) v.bank];
                const double rate = std::pow (2.0, s.pitch.load() / 12.0) * set->sampleRate / deviceRate;
                const float vol = s.vol.load() * v.velocity, A = std::max (0.001f, s.attack.load()), D = std::max (0.005f, s.decay.load());
                const float S = s.sustain.load(), R = s.release.load();
                const float* L = buf.getReadPointer (0); const float* Rr = buf.getReadPointer (1);
                const double sr = deviceRate;
                const double decayCoef = std::exp (-1.0 / ((D / 3.0) * sr));
                for (int i = 0; i < num; ++i)
                {
                    if (! v.loop && v.pos >= len - 1) { v.active = false; break; }
                    if (v.loop && v.pos >= len) v.pos -= len;
                    // inviluppo
                    switch (v.stage)
                    {
                        case Stage::attack:  v.level += (float) (1.0 / (A * sr)); if (v.level >= 1.f) { v.level = 1.f; v.stage = Stage::decay; } break;
                        case Stage::decay:   v.level = (float) (S + (v.level - S) * decayCoef); break;
                        case Stage::release: v.level -= v.releaseStep; if (v.level <= 0.f) { v.active = false; } break;
                    }
                    if (! v.active) break;
                    if (! v.loop && v.stage != Stage::release)
                    {
                        const double remainingOut = (len - 1 - v.pos) / rate / sr;   // secondi alla fine del campione
                        const double relT = std::min ((double) R, (len / rate / sr) * 0.5);
                        if (remainingOut <= relT && relT > 0) startRelease (v, (float) std::max (remainingOut, 0.0005));
                    }
                    const int i0 = (int) v.pos; const float fr = (float) (v.pos - i0);
                    const int i1 = i0 + 1 < len ? i0 + 1 : (v.loop ? 0 : i0);
                    const float g = v.level * vol;
                    out.addSample (0, start + i, (L[i0] + (L[i1] - L[i0]) * fr) * g);
                    out.addSample (1, start + i, (Rr[i0] + (Rr[i1] - Rr[i0]) * fr) * g);
                    v.pos += rate;
                }
                if (v.active && ! v.forcedFade) ph[(size_t) v.pad] = (float) std::fmod (v.pos / len, 1.0);
                if (! v.active && v.loop && ! v.forcedFade) latched[(size_t) v.pad] = false;
            }
        for (int i = 0; i < numPads; ++i) playhead[(size_t) i].store (ph[(size_t) i], std::memory_order_relaxed);
    }

    std::array<BankSettings, numBanks> settings;
    std::array<std::atomic<float>, numPads> playhead;   // posizione 0..1 o -1, per la linea sui pad
    std::array<std::atomic<bool>, numPads> latched;

private:
    enum class Stage { attack, decay, release };
    struct Voice
    {
        bool active = false, loop = false, forcedFade = false;
        int pad = 0, bank = 0;
        double pos = 0;
        float level = 0, velocity = 1, releaseStep = 0;
        Stage stage = Stage::attack;
    };
    static constexpr int maxVoices = 64;
    std::array<Voice, maxVoices> voices;
    double deviceRate = 44100;

    void startRelease (Voice& v, float seconds)
    {
        v.stage = Stage::release;
        v.releaseStep = v.level / (float) std::max (1.0, seconds * deviceRate);
        if (v.releaseStep <= 0) v.active = false;
    }
    void fadeOut (int pad, float seconds)
    {
        for (auto& v : voices)
            if (v.active && v.pad == pad) { startRelease (v, seconds); v.forcedFade = true; v.loop = false; }
    }
    Voice* freeVoice()
    {
        for (auto& v : voices) if (! v.active) return &v;
        Voice* q = &voices[0];
        for (auto& v : voices) if (v.level < q->level) q = &v;   // ruba la piu' debole
        return q;
    }
};
}  // namespace sf
