// Sample Forge · MBStudio91 — LED dei pad sui controller (Push 2/3, Launchpad, Launchkey)
//
// Scrive i LED sulla porta MIDI OUT del controller e legge pad e tasti banco dalla porta IN.
// Per Push 2 la lettura dei pad non serve qui: arrivano gia' come note MIDI normali.
// I LED si rimandano tutti ogni 5 s e, nel resto del tempo, solo i cambi.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <functional>

class ControllerLeds : private juce::Timer, private juce::MidiInputCallback
{
public:
    enum class Kind { none, push, launchpadMk3, launchpadClassic, launchkeyMk3, launchkeyMk2, launchkeyMk1 };
    enum class PadState : uint8_t { empty, loaded, playing, latched };
    enum class Col : uint8_t { off, dim, blue, green, amber };

    struct Host
    {
        std::function<PadState (int pad)> padState;     // 0..39
        std::function<int()> currentBank;               // banco mostrato nell'interfaccia
        std::function<void (int pad)> padDown, padUp;   // pad premuti sul controller
        std::function<void (int bank)> selectBank;      // tasti banco premuti sul controller
    };

    explicit ControllerLeds (Host);
    ~ControllerLeds() override;

    void setEnabled (bool);
    bool isEnabled() const { return enabled; }
    /** Nome del controller collegato ("" se nessuno). */
    juce::String getDeviceName() const { return deviceName; }
    Kind getKind() const { return kind; }

    /** True se questa nota e' appena arrivata da un controller che Sample Forge gestisce:
        il processore la salta, cosi' un pad non suona due volte (una dal controller, una dalla porta MIDI). */
    bool claimedRecently (int note) const;

    // ---- traduzione (statiche, usate anche dai test)
    static Kind detect (const juce::String& portName);
    static juce::String kindName (Kind);
    /** Pad 0..39 -> nota LED del controller; -1 se non visibile con il banco mostrato. */
    static int padNote (Kind, int pad, int shownBank);
    /** Banco 0..4 -> nota LED del tasto banco; -1 se non c'e'. */
    static int bankNote (Kind, int bank);
    /** Nota in ingresso -> pad 0..39, banco come -1 - b, oppure -100 se non riconosciuta. */
    static int decodeInput (Kind, int note, int shownBank);
    /** Valore di velocity per il colore (non usato da Launchpad MK3, che va in RGB). */
    static int paletteValue (Kind, Col);
    /** Colore di un pad in base allo stato. */
    static Col padColour (PadState, bool inShownBank);

private:
    void timerCallback() override;
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
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
    std::unique_ptr<juce::MidiInput> in;
    std::array<std::atomic<uint32_t>, 128> claimedAt {};   // millisecondi + 1, 0 = mai
    std::array<int, 128> sent {};        // ultimo colore inviato per nota (-1 = da inviare)
    int scanCountdown = 0, refreshCountdown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControllerLeds)
};
