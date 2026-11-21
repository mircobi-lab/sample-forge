// Sample Forge · MBStudio91 — motore DSP nativo (C++17, nessuna dipendenza)
// Porting fedele del motore JavaScript: separazione approssimativa a 4 sorgenti
// (batteria / voce / basso / altro), BPM, tonalità, accordi, scelta e ritaglio dei campioni.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sf
{

struct Stereo
{
    std::vector<float> L, R;
    size_t size() const { return L.size(); }
};

struct Pad
{
    std::string label, sub, kind;  // kind: drum, vox, loop, chord, bass
    Stereo audio;                  // normalizzato a -1 dBFS
    double start = 0, dur = 0;     // secondi nel brano
    int bars = 0;
    bool loop = false;
    std::string cls, chord, note;
    int midi = -1;
};

struct Bank
{
    std::string id;  // drums, vox, loops, chords, bass
    std::array<std::shared_ptr<Pad>, 8> pads;
};

struct KeyInfo
{
    int root = 0;
    bool minor = false, flats = false;
    std::string name, it;  // "Am", "La minore"
    double conf = 0;
};

struct Result
{
    double sr = 44100, dur = 0, cappedSec = 0, bpm = 120, beat0 = 0, T = 0.5;
    int nBars = 0;
    KeyInfo key;
    std::vector<std::string> progression;
    bool fromStems = false;
    std::array<Bank, 5> banks;
};

/** Sorgenti: o il mix (separazione interna) o quattro stem già separati. */
struct Sources
{
    bool stems = false;
    Stereo mix;                          // modalità mix
    Stereo drums, vocals, bass, other;   // modalità stem (stessa lunghezza)
};

using Progress = std::function<void (double fraction, const std::string& message)>;

class Engine
{
public:
    Engine();
    ~Engine();

    /** Analisi completa. Lancia std::runtime_error in caso di problemi; si interrompe se cancel diventa true. */
    Result analyze (Sources src, double sampleRate, double cappedSec, Progress progress, const std::atomic<bool>* cancel = nullptr);

    /** Nuova griglia con un BPM imposto e nuovo ritaglio (dopo analyze). */
    Result reslice (double bpm, Progress progress, const std::atomic<bool>* cancel = nullptr);

    /** Stem completo ricostruito: drums, vocals, bass, chords, harm, inst, mix. */
    Stereo synthFull (const std::string& name) const;

    /** Parametri del separatore (per il banco di prova). */
    struct Params
    {
        double q = 1, csharp = 1.5, cSmoothT = 0.55;
        int cSmoothF = 2, medS = 17, medL = 171, medLstep = 4, medP = 17;
        double pMargin = 2, fluctW = 1.0, sustW = 0.4;
        int smoothT = 1, smoothF = 1;
        double smoothLoHz = 400, sharp = 1.3, vGateLo = 0.02, vGateHi = 0.10, vGateWin = 0.15, kickHz = 130, supLo = 0.15, supHi = 0.6;
    };
    Params params;

    // per i test
    Stereo synthRange (const std::string& name, long s0, long s1) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

}  // namespace sf
