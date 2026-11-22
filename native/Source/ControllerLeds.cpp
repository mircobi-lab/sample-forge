// Sample Forge · MBStudio91 — LED dei pad sui controller
//
// Fonti (manuali dei costruttori):
//  - Push 2: Ableton "Push 2 MIDI and Display Interface Manual" (github.com/Ableton/push-interface):
//    pad = note 36 (in basso a sinistra) ... 99 (in alto a destra), colore = velocity (indice tavolozza).
//    Tavolozza usata qui: 127 = rosso (verificato dall'esempio della libreria node-ableton-push2);
//    gli altri indici (124-126) sono da verificare sulla tabella colori del manuale.
//  - Launchpad Mini MK3: Programmer's Reference. Modo Programmer F0 00 20 29 02 0D 0E 01 F7,
//    ritorno a Live ... 0E 00 F7; pad = 10 x riga + colonna (11 in basso a sinistra);
//    colore RGB con SysEx F0 00 20 29 02 0D 03 (03 led r g b)... F7, valori 0-127.
//  - Launchpad / Launchpad S / Mini MK1-MK2: Programmer's Reference. Layout X-Y, nota = 16 x riga + colonna
//    (0 in alto a sinistra); velocity = 16 x verde + rosso + 12; reset B0 00 00.
//  - Launchkey MK3: Programmer's Reference. Modo DAW 9F 0C 7F / 9F 0C 00 sulla porta DAW,
//    layout Session BF 03 02; pad riga alta 96-103, riga bassa 112-119, canale 1, velocity = colore.
//  - Launchkey MK2: Programmer's Reference. Modo InControl 9F 0C 7F / 00 sulla porta InControl,
//    pad 96-103 e 112-119, luce con 9F nota colore.
//  - Launchkey MK1: stesso schema dell'MK2 con colori rosso/verde tipo Launchpad (da verificare).
#include "ControllerLeds.h"

using Kind = ControllerLeds::Kind;
using Col = ControllerLeds::Col;

namespace
{
    bool has (const juce::String& s, const char* t) { return s.contains (t); }

    struct Rgb { uint8_t r, g, b; };
    Rgb rgbFor (Col c)
    {
        switch (c)
        {
            case Col::dim:   return { 10, 10, 12 };
            case Col::blue:  return { 8, 40, 127 };   // blu LED MBStudio (#3d8bff)
            case Col::green: return { 16, 127, 30 };  // verde (#3ee05c)
            case Col::amber: return { 127, 70, 0 };   // ambra (#ffb020)
            case Col::off: default: return { 0, 0, 0 };
        }
    }
}

//==============================================================================
Kind ControllerLeds::detect (const juce::String& portName)
{
    const auto s = portName.toLowerCase();
    const bool port2 = has (s, "midiout2") || has (s, "midiin2") || has (s, "port 2");

    if (has (s, "push 2") || has (s, "push2") || has (s, "push 3") || has (s, "push3"))
        return (has (s, "user") || port2) ? Kind::push : Kind::none;

    if (has (s, "lpminimk3") || has (s, "launchpad mini mk3"))
        return (! has (s, "daw") && has (s, "midi")) ? Kind::launchpadMk3 : Kind::none;

    if (has (s, "launchkey") || has (s, "lkmk3") || has (s, "lk mini"))
    {
        if (has (s, "mk3") || has (s, "lkmk3"))
            return (has (s, "daw") || port2) ? Kind::launchkeyMk3 : Kind::none;
        if (has (s, "incontrol") || port2)
            return has (s, "mk2") ? Kind::launchkeyMk2 : Kind::launchkeyMk1;
        return Kind::none;
    }

    if (has (s, "launchpad") && ! has (s, "mk3") && ! has (s, "pro") && ! has (s, "launchpad x")
        && ! has (s, "launchpad mk2"))
        return Kind::launchpadClassic;

    return Kind::none;
}

juce::String ControllerLeds::kindName (Kind k)
{
    switch (k)
    {
        case Kind::push:             return "PUSH (USER)";
        case Kind::launchpadMk3:     return "LAUNCHPAD MINI MK3";
        case Kind::launchpadClassic: return "LAUNCHPAD";
        case Kind::launchkeyMk3:     return "LAUNCHKEY MK3";
        case Kind::launchkeyMk2:     return "LAUNCHKEY MK2";
        case Kind::launchkeyMk1:     return "LAUNCHKEY";
        case Kind::none: default:    return {};
    }
}

// Disposizione: sulle griglie 8x8 i 5 banchi sono le 5 righe dal basso (banco 1 in basso,
// come le note 36-75 del Push), la riga in alto accende i tasti banco 1-5.
// Sui Launchkey (2 righe da 8): riga bassa = pad del banco mostrato, riga alta = tasti banco.
int ControllerLeds::padNote (Kind k, int pad, int shownBank)
{
    if (pad < 0 || pad >= 40) return -1;
    const int b = pad / 8, c = pad % 8;
    switch (k)
    {
        case Kind::push:             return 36 + pad;
        case Kind::launchpadMk3:     return 10 * (b + 1) + c + 1;
        case Kind::launchpadClassic: return 16 * b + c;          // X-Y: riga 0 in basso
        case Kind::launchkeyMk3:
        case Kind::launchkeyMk2:
        case Kind::launchkeyMk1:     return b == shownBank ? 112 + c : -1;
        case Kind::none: default:    return -1;
    }
}

int ControllerLeds::bankNote (Kind k, int bank)
{
    if (bank < 0 || bank >= 5) return -1;
    switch (k)
    {
        case Kind::push:             return 92 + bank;     // riga in alto (92-99)
        case Kind::launchpadMk3:     return 81 + bank;
        case Kind::launchpadClassic: return 16 * bank + 8;      // colonna delle scene a destra
        case Kind::launchkeyMk3:
        case Kind::launchkeyMk2:
        case Kind::launchkeyMk1:     return 96 + bank;
        case Kind::none: default:    return -1;
    }
}

int ControllerLeds::decodeInput (Kind k, int note, int shownBank)
{
    for (int b = 0; b < 5; ++b) if (bankNote (k, b) == note) return -1 - b;
    for (int p = 0; p < 40; ++p) if (padNote (k, p, shownBank) == note) return p;
    return -100;
}

int ControllerLeds::paletteValue (Kind k, Col c)
{
    switch (k)
    {
        case Kind::push:
        {
            static const int v[] = { 0, 124, 125, 126, 127 };   // 124-126 da verificare
            return v[(int) c];
        }
        case Kind::launchpadClassic:
        case Kind::launchkeyMk1:
        {
            // Launchpad classico: spento 12, verde basso 28, verde pieno 60, ambra piena 63, rosso pieno 15.
            // Non c'e' il blu: il colore "pagina attiva" usa l'ambra.   (Launchkey MK1: da verificare)
            static const int v[] = { 12, 28, 63, 60, 15 };
            return v[(int) c];
        }
        case Kind::launchkeyMk3:
        {
            // Dal manuale Launchkey MK3: spento 0, rosso 5, verde 19, blu 45. Grigio (dim) e giallo (ambra)
            // sono da verificare: uso 1 e 13 (il giallo compare nell'esempio del manuale Launchpad).
            static const int v[] = { 0, 1, 45, 19, 13 };
            return v[(int) c];
        }
        case Kind::launchkeyMk2:
        {
            // Stessa tavolozza del Launchpad MK2 (da verificare sulla tabella colori).
            static const int v[] = { 0, 1, 45, 19, 13 };
            return v[(int) c];
        }
        case Kind::launchpadMk3:
        case Kind::none: default: return 0;
    }
}

Col ControllerLeds::padColour (PadState s, bool inShownBank)
{
    switch (s)
    {
        case PadState::playing: return Col::green;
        case PadState::latched: return Col::amber;
        case PadState::loaded:  return inShownBank ? Col::blue : Col::dim;
        case PadState::empty: default: return Col::off;
    }
}

//==============================================================================
ControllerLeds::ControllerLeds (Host h) : host (std::move (h))
{
    sent.fill (-1);
}

ControllerLeds::~ControllerLeds()
{
    stopTimer();
    close();
}

void ControllerLeds::setEnabled (bool e)
{
    if (e == enabled) return;
    enabled = e;
    if (enabled) { scanCountdown = 0; startTimerHz (25); }
    else { stopTimer(); close(); }
}

void ControllerLeds::timerCallback()
{
    if (--scanCountdown <= 0) { scanCountdown = 50; scan(); }
    if (out != nullptr)
    {
        const bool force = --refreshCountdown <= 0;
        if (force) refreshCountdown = 125;                  // ogni 5 s rimanda tutto (controller ricollegato)
        refresh (force);
    }
}

void ControllerLeds::scan()
{
    const auto outs = juce::MidiOutput::getAvailableDevices();
    if (out != nullptr)
    {
        for (auto& d : outs) if (d.identifier == outId) return;   // ancora collegato
        close();
    }
    // ordine di preferenza: Push, Launchpad, Launchkey
    for (auto want : { Kind::push, Kind::launchpadMk3, Kind::launchpadClassic, Kind::launchkeyMk3, Kind::launchkeyMk2, Kind::launchkeyMk1 })
        for (auto& d : outs)
            if (detect (d.name) == want) { open (want, d); return; }
}

void ControllerLeds::open (Kind k, const juce::MidiDeviceInfo& outInfo)
{
    out = juce::MidiOutput::openDevice (outInfo.identifier);
    if (out == nullptr) return;
    kind = k; outId = outInfo.identifier; deviceName = kindName (k);
    sent.fill (-1);

    // Ingresso: stesso controller, porta IN con lo stesso riconoscimento. Push 2 non passa di qui.
    if (k != Kind::push)
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (detect (d.name) == k)
            {
                in = juce::MidiInput::openDevice (d.identifier, this);
                if (in != nullptr) in->start();
                break;
            }

    sendModeOn();
    refresh (true);
}

void ControllerLeds::close()
{
    if (in != nullptr) { in->stop(); in.reset(); }
    if (out != nullptr)
    {
        std::vector<std::pair<int, Col>> offs;
        for (int n = 0; n < 128; ++n) if (sent[(size_t) n] > 0) offs.push_back ({ n, Col::off });
        sendNotes (offs);
        sendModeOff();
        out.reset();
    }
    kind = Kind::none; outId = {}; deviceName = {};
    sent.fill (-1);
}

void ControllerLeds::sendModeOn()
{
    if (out == nullptr) return;
    switch (kind)
    {
        case Kind::launchpadMk3:
        {
            const uint8_t prog[] = { 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x01 };
            out->sendMessageNow (juce::MidiMessage::createSysExMessage (prog, (int) sizeof (prog)));
            break;
        }
        case Kind::launchpadClassic:
            out->sendMessageNow (juce::MidiMessage::controllerEvent (1, 0, 0));   // reset
            out->sendMessageNow (juce::MidiMessage::controllerEvent (1, 0, 1));   // layout X-Y
            break;
        case Kind::launchkeyMk3:
            out->sendMessageNow (juce::MidiMessage::noteOn (16, 12, (juce::uint8) 127));  // modo DAW
            out->sendMessageNow (juce::MidiMessage::controllerEvent (16, 3, 2));          // pad in Session
            break;
        case Kind::launchkeyMk2:
        case Kind::launchkeyMk1:
            out->sendMessageNow (juce::MidiMessage::noteOn (16, 12, (juce::uint8) 127));  // InControl
            break;
        case Kind::push:
        case Kind::none: default: break;   // Push: l'utente preme USER
    }
}

void ControllerLeds::sendModeOff()
{
    if (out == nullptr) return;
    switch (kind)
    {
        case Kind::launchpadMk3:
        {
            const uint8_t live[] = { 0x00, 0x20, 0x29, 0x02, 0x0D, 0x0E, 0x00 };
            out->sendMessageNow (juce::MidiMessage::createSysExMessage (live, (int) sizeof (live)));
            break;
        }
        case Kind::launchpadClassic:
            out->sendMessageNow (juce::MidiMessage::controllerEvent (1, 0, 0));
            break;
        case Kind::launchkeyMk3:
        case Kind::launchkeyMk2:
        case Kind::launchkeyMk1:
            out->sendMessageNow (juce::MidiMessage::noteOn (16, 12, (juce::uint8) 0));
            break;
        case Kind::push:
        case Kind::none: default: break;
    }
}

void ControllerLeds::sendNotes (const std::vector<std::pair<int, Col>>& changes)
{
    if (out == nullptr || changes.empty()) return;
    if (kind == Kind::launchpadMk3)
    {
        std::vector<uint8_t> d { 0x00, 0x20, 0x29, 0x02, 0x0D, 0x03 };
        for (auto& [note, c] : changes)
        {
            const auto rgb = rgbFor (c);
            d.insert (d.end(), { 0x03, (uint8_t) note, rgb.r, rgb.g, rgb.b });
        }
        out->sendMessageNow (juce::MidiMessage::createSysExMessage (d.data(), (int) d.size()));
        return;
    }
    const int channel = (kind == Kind::launchkeyMk2 || kind == Kind::launchkeyMk1) ? 16 : 1;
    for (auto& [note, c] : changes)
        out->sendMessageNow (juce::MidiMessage::noteOn (channel, note, (juce::uint8) paletteValue (kind, c)));
}

void ControllerLeds::refresh (bool force)
{
    if (! host.padState || ! host.currentBank) return;
    const int bank = juce::jlimit (0, 4, host.currentBank());

    std::array<int, 128> want; want.fill (0);
    std::array<bool, 5> bankHas {};
    for (int p = 0; p < 40; ++p)
    {
        const auto st = host.padState (p);
        if (st != PadState::empty) bankHas[(size_t) (p / 8)] = true;
        const int n = padNote (kind, p, bank);
        if (n >= 0 && n < 128) want[(size_t) n] = 1 + (int) padColour (st, p / 8 == bank);
    }
    for (int b = 0; b < 5; ++b)
    {
        const int n = bankNote (kind, b);
        if (n >= 0 && n < 128) want[(size_t) n] = 1 + (int) (b == bank ? Col::amber : bankHas[(size_t) b] ? Col::dim : Col::off);
    }

    std::vector<std::pair<int, Col>> changes;
    for (int n = 0; n < 128; ++n)
    {
        const int w = want[(size_t) n];
        if (w == 0) continue;                               // nota non usata da Sample Forge
        const int v = w - 1;
        if (force || sent[(size_t) n] != v) { changes.push_back ({ n, (Col) v }); sent[(size_t) n] = v; }
    }
    sendNotes (changes);
}


void ControllerLeds::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (! (m.isNoteOn() || m.isNoteOff())) return;
    const int code = decodeInput (kind, m.getNoteNumber(), host.currentBank ? host.currentBank() : 0);
    if (code == -100) return;
    const bool down = m.isNoteOn();
    if (code < 0)
    {
        if (down && host.selectBank) host.selectBank (-1 - code);
        return;
    }
    if (down) { if (host.padDown) host.padDown (code); }
    else if (host.padUp) host.padUp (code);
}
