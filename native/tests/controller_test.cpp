// Test della mappatura dei controller (LED e ingressi). Nessun hardware richiesto.
#include "../Source/ControllerLeds.h"
#include <set>

namespace
{
    int failures = 0, checks = 0;
    void check (bool ok, const char* what)
    {
        ++checks;
        if (! ok) { ++failures; std::printf ("FAIL  %s\n", what); }
    }
}

using Kind = ControllerLeds::Kind;
using Col = ControllerLeds::Col;
using PS = ControllerLeds::PadState;

int main()
{
    const Kind kinds[] = { Kind::push, Kind::launchpadMk3, Kind::launchpadClassic,
                           Kind::launchkeyMk3, Kind::launchkeyMk2, Kind::launchkeyMk1 };

    // 1. Riconoscimento dei nomi porta
    check (ControllerLeds::detect ("Ableton Push 2 User Port") == Kind::push, "Push 2 porta USER riconosciuta");
    check (ControllerLeds::detect ("Ableton Push 2 Live Port") == Kind::none, "Push 2 porta LIVE ignorata");
    check (ControllerLeds::detect ("Launchpad Mini MK3 MIDI") == Kind::launchpadMk3, "Launchpad Mini MK3 riconosciuto");
    check (ControllerLeds::detect ("Launchpad Mini MK3 DAW") == Kind::none, "Launchpad MK3 porta DAW ignorata");
    check (ControllerLeds::detect ("Launchkey MK3 49 DAW") == Kind::launchkeyMk3, "Launchkey MK3 porta DAW riconosciuta");
    check (ControllerLeds::detect ("Keystation 88 MK3") == Kind::none, "tastiera generica ignorata");
    check (ControllerLeds::detect ("Launchpad Pro MK3") == Kind::none, "Launchpad Pro non supportato");

    // 2. Ogni pad va e torna: padNote -> decodeInput deve restituire lo stesso pad
    for (auto k : kinds)
        for (int shown = 0; shown < 5; ++shown)
            for (int p = 0; p < 40; ++p)
            {
                const int n = ControllerLeds::padNote (k, p, shown);
                if (n < 0) continue;
                check (ControllerLeds::decodeInput (k, n, shown) == p, "pad -> nota -> pad");
            }

    // 3. Tasti banco: bankNote -> decodeInput deve restituire -1 - banco
    for (auto k : kinds)
        for (int b = 0; b < 5; ++b)
        {
            const int n = ControllerLeds::bankNote (k, b);
            if (n < 0) { check (k == Kind::none, "banco senza nota solo su Kind::none"); continue; }
            check (ControllerLeds::decodeInput (k, n, 0) == -1 - b, "banco -> nota -> banco");
        }

    // 4. Nessuna collisione: tasti banco e pad visibili devono avere note diverse
    for (auto k : kinds)
        for (int shown = 0; shown < 5; ++shown)
        {
            std::set<int> seen;
            bool dup = false;
            for (int p = 0; p < 40; ++p) { const int n = ControllerLeds::padNote (k, p, shown); if (n >= 0 && ! seen.insert (n).second) dup = true; }
            for (int b = 0; b < 5; ++b) { const int n = ControllerLeds::bankNote (k, b); if (n >= 0 && ! seen.insert (n).second) dup = true; }
            check (! dup, "nessuna nota doppia tra pad e tasti banco");
        }

    // 5. Intervalli di nota MIDI validi (0..127)
    for (auto k : kinds)
        for (int p = 0; p < 40; ++p)
        {
            const int n = ControllerLeds::padNote (k, p, 0);
            check (n < 128, "nota pad < 128");
        }

    // 6. Note attese per tipo di controller
    check (ControllerLeds::padNote (Kind::push, 0, 0) == 36, "Push pad 0 = nota 36 (in basso a sinistra)");
    check (ControllerLeds::padNote (Kind::push, 39, 0) == 75, "Push pad 39 = nota 75");
    check (ControllerLeds::padNote (Kind::launchpadMk3, 0, 0) == 11, "Launchpad MK3 pad 0 = nota 11");
    check (ControllerLeds::padNote (Kind::launchpadMk3, 39, 0) == 58, "Launchpad MK3 pad 39 = nota 58");
    check (ControllerLeds::padNote (Kind::launchkeyMk3, 0, 0) == 112, "Launchkey pad 0 = nota 112 (banco 0 mostrato)");
    check (ControllerLeds::padNote (Kind::launchkeyMk3, 0, 1) == -1, "Launchkey: banco non mostrato senza pad");

    // 7. Colori dei pad
    check (ControllerLeds::padColour (PS::empty, true) == Col::off, "pad vuoto spento");
    check (ControllerLeds::padColour (PS::loaded, true) == Col::blue, "pad caricato nel banco mostrato: blu");
    check (ControllerLeds::padColour (PS::loaded, false) == Col::dim, "pad caricato fuori banco: dim");
    check (ControllerLeds::padColour (PS::playing, false) == Col::green, "pad in riproduzione: verde");
    check (ControllerLeds::padColour (PS::latched, true) == Col::amber, "pad latch: ambra");

    // 8. Indici di tavolozza nel range MIDI
    for (auto k : kinds)
        for (int c = 0; c < 5; ++c)
            check (ControllerLeds::paletteValue (k, (Col) c) >= 0 && ControllerLeds::paletteValue (k, (Col) c) < 128,
                   "indice tavolozza nel range MIDI");

    std::printf ("\n%d controlli, %d falliti\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
