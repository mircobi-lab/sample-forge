// Sample Forge · MBStudio91 — LED dei pad sui controller (Push 2/3, Launchpad, Launchkey)
//
// Solo uscita: scrive i LED sulla porta MIDI OUT del controller. Non legge i pad
// (quelli restano sul canale MIDI normale). Gira sul thread dei messaggi.
// Ordine di scrittura: si rimandano tutti i LED ogni 5 s e solo i cambi nel resto del tempo.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <functional>

class ControllerLeds : private juce::Timer
{
public:
    enum class Kind { none, push, launchpadMk3, launchpadClassic, launchkeyMk3, launchkeyMk2, launchkeyMk1 };
    enum class PadState : uint8_t { empty, loaded, playing, latched };
    enum class Col : uint8_t { off, dim, blue, green, amber };

    struct Host
    {
        std::function<PadState (int pad)> padState;     // 0..39
        std::function<int()> currentBank;               // banco mostrato nell'interfaccia
        };

    explicit ControllerLeds (Host);
    ~ControllerLeds() override;

    void setEnabled (bool);
    bool isEnabled() const { return enabled; }
    /** Nome del controller collegato ("" se nessuno). */
    juce::String getDeviceName() const { return deviceName; }
    Kind getKind() const { return kind; }

    // ---- traduzione (statiche, usate anche dai test)
    static Kind detect (const juce::String& portName);
    static juce::String kindName (Kind);
    /** Pad 0..39 -> nota LED del controller; -1 se non visibile con il banco mostrato. */
    static int padNote (Kind, int pad, int shownBank);
    /** Banco 0..4 -> nota LED del tasto banco; -1 se non c'e'. */
    static int bankNote (Kind, int bank);
    /** Valore di velocity per il colore (non usato da Launchpad MK3, che va in RGB). */
    static int paletteValue (Kind, Col);
    /** Colore di un pad in base allo stato. */
    static Col padColour (PadState, bool inShownBank);

private:
    void timerCallback() override;
    void scan();
    void open (Kind, const juce::MidiDeviceInfo& outInfo);
    void close();
    void refresh (bool force);
    void sendModeOn();
    void sendModeOff();
    void sendNotes (const std::vector<std::pair<int, Col>>&);

    Host host;
    bool enabled = false;
    Kind kind = Kind::none;
    juce::String deviceName, outId;
    std::unique_ptr<juce::MidiOutput> out;
    std::array<int, 128> sent {};        // ultimo colore inviato per nota (-1 = da inviare)
    int scanCountdown = 0, refreshCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControllerLeds)
};
