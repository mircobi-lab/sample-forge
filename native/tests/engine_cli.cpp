// Banco di prova del motore nativo: legge un brano di prova (float32 grezzi, vedi dump_songs.mjs),
// analizza il mix, stampa BPM, tonalita', accordi e SDR di ogni stem ricostruito.
// Uso: engine_cli <cartella brano> [--stems] [--expect-bpm X --expect-key Am --min-sdr 2.0]
#include "../Source/Engine/SampleForgeEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

static std::vector<float> readF32 (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) { std::fprintf (stderr, "manca %s\n", path.c_str()); std::exit (2); }
    f.seekg (0, std::ios::end); const auto n = (size_t) f.tellg() / 4; f.seekg (0);
    std::vector<float> v (n); f.read ((char*) v.data(), (std::streamsize) (n * 4));
    return v;
}
static sf::Stereo readStereo (const std::string& dir, const std::string& k)
{
    return { readF32 (dir + "/" + k + "_L.f32"), readF32 (dir + "/" + k + "_R.f32") };
}
static double sdr (const sf::Stereo& ref, const sf::Stereo& est)
{
    double s = 0, e = 0;
    for (size_t i = 0; i < ref.L.size(); ++i)
    {
        const double l = i < est.L.size() ? est.L[i] : 0, r = i < est.R.size() ? est.R[i] : 0;
        s += (double) ref.L[i] * ref.L[i] + (double) ref.R[i] * ref.R[i];
        e += (ref.L[i] - l) * (ref.L[i] - l) + (ref.R[i] - r) * (ref.R[i] - r);
    }
    return 10 * std::log10 (s / (e + 1e-12));
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::fprintf (stderr, "uso: engine_cli <cartella> [--stems] [--expect-bpm X] [--expect-key K] [--min-sdr D]\n"); return 2; }
    const std::string dir = argv[1];
    bool useStems = false; double expBpm = 0, minSdr = -99; std::string expKey;
    for (int i = 2; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--stems")) useStems = true;
        else if (! std::strcmp (argv[i], "--expect-bpm") && i + 1 < argc) expBpm = std::atof (argv[++i]);
        else if (! std::strcmp (argv[i], "--expect-key") && i + 1 < argc) expKey = argv[++i];
        else if (! std::strcmp (argv[i], "--min-sdr") && i + 1 < argc) minSdr = std::atof (argv[++i]);
    }
    double sr = 44100; { std::ifstream f (dir + "/info.txt"); f >> sr; }
    sf::Stereo drums = readStereo (dir, "drums"), vocals = readStereo (dir, "vocals"), bass = readStereo (dir, "bass"), other = readStereo (dir, "other");
    sf::Sources src;
    if (useStems) { src.stems = true; src.drums = drums; src.vocals = vocals; src.bass = bass; src.other = other; }
    else src.mix = readStereo (dir, "mix");

    sf::Engine eng;
    const auto t0 = std::chrono::steady_clock::now();
    sf::Result r = eng.analyze (src, sr, 0, nullptr);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    std::string prog;
    for (size_t i = 0; i < r.progression.size() && i < 8; ++i) prog += r.progression[i] + " ";
    int pads = 0; for (auto& b : r.banks) for (auto& p : b.pads) if (p) pads++;
    std::printf ("analisi %.1fs  bpm %g key %s  prog %s pad %d\n", secs, r.bpm, r.key.name.c_str(), prog.c_str(), pads);
    for (auto& b : r.banks)
    {
        std::printf ("  %-7s", b.id.c_str());
        for (auto& p : b.pads) std::printf (" | %s", p ? (p->label + " (" + std::to_string (p->dur).substr (0, 4) + ")").c_str() : "-");
        std::printf ("\n");
    }
    bool fail = false;
    if (! useStems)
    {
        sf::Stereo inst; inst.L.assign (drums.L.size(), 0); inst.R.assign (drums.L.size(), 0);
        for (auto* s : { &drums, &bass, &other }) for (size_t i = 0; i < inst.L.size(); ++i) { inst.L[i] += s->L[i]; inst.R[i] += s->R[i]; }
        const double d = sdr (drums, eng.synthFull ("drums")), v = sdr (vocals, eng.synthFull ("vocals")),
                     b = sdr (bass, eng.synthFull ("bass")), c = sdr (other, eng.synthFull ("chords")), in = sdr (inst, eng.synthFull ("inst"));
        std::printf ("SDR dB  batteria %6.2f  voce %6.2f  basso %6.2f  accordi %6.2f  strumentale %6.2f\n", d, v, b, c, in);
        if ((d + v + b + c) / 4 < minSdr) { std::printf ("FALLITO: SDR medio sotto %.2f\n", minSdr); fail = true; }
    }
    if (expBpm > 0 && std::abs (r.bpm - expBpm) > 0.5) { std::printf ("FALLITO: BPM %g invece di %g\n", r.bpm, expBpm); fail = true; }
    if (! expKey.empty() && r.key.name != expKey) { std::printf ("FALLITO: tonalita' %s invece di %s\n", r.key.name.c_str(), expKey.c_str()); fail = true; }
    if (pads < 25) { std::printf ("FALLITO: solo %d pad\n", pads); fail = true; }
    std::printf (fail ? "ESITO: FALLITO\n" : "ESITO: OK\n");
    return fail ? 1 : 0;
}
