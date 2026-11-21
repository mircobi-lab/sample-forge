// Sample Forge · MBStudio91 — processore (Standalone / VST3 / AU)
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Sampler.h"
#include "Engine/SampleForgeEngine.h"

class SampleForgeProcessor : public juce::AudioProcessor, private juce::Thread, private juce::AsyncUpdater
{
public:
    SampleForgeProcessor();
    ~SampleForgeProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Sample Forge"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- comandi dall'interfaccia (thread dei messaggi)
    void padDown (int pad);
    void padUp (int pad);
    void requestPanic() { panicRequested = true; }
    void playOriginal (double fromSeconds);   // < 0 ferma
    bool isPlayingOriginal() const { return originalPos.load() >= 0; }
    double originalPositionSeconds() const;

    /** Carica uno o piu' file: un mix (separazione interna) o gli stem (riconosciuti dal nome). */
    void loadFiles (const juce::Array<juce::File>& files);
    void reslice (double bpm);
    /** Esporta il pacchetto zip in un file (in background). */
    struct ExportOptions { int bits = 24; bool copy432 = false; bool stems = false; };
    void exportPack (const juce::File& zipFile, ExportOptions);

    // ---- stato per l'interfaccia (thread dei messaggi)
    struct Status { bool busy = false; double progress = -1; juce::String message; };
    Status getStatus() const;
    std::shared_ptr<const sf::Result> getResult() const { return result; }
    juce::String getSongName() const { return songName; }
    double getSongDuration() const { return songDuration; }
    const std::vector<float>& getOverview() const { return overview; }
    bool hasSong() const { return songDuration > 0; }
    std::function<void()> onStateChanged;      // risultato nuovo, errore, fine export (thread dei messaggi)
    std::function<void (juce::String)> onToast;

    sf::Sampler sampler;

private:
    // ---- lavori in background
    enum class Job { none, analyze, reslice, exportZip };
    void run() override;
    void handleAsyncUpdate() override;
    void setStatus (bool busy, double p, const juce::String& msg);
    void doAnalyze();
    void doExport();
    void installResult (std::shared_ptr<sf::Result>);

    juce::AudioFormatManager formats;
    sf::Engine engine;
    std::atomic<bool> cancel { false };
    Job job = Job::none;
    juce::Array<juce::File> pendingFiles;
    double pendingBpm = 0;
    juce::File pendingZip;
    ExportOptions pendingExport;
    juce::String jobError, jobInfo;
    std::shared_ptr<sf::Result> pendingResult;
    juce::String pendingSongName;
    std::vector<float> pendingOverview;

    mutable juce::CriticalSection statusLock;
    Status status;

    std::shared_ptr<const sf::Result> result;
    juce::String songName;
    double songDuration = 0;
    std::vector<float> overview;
    juce::StringArray stemMap;

    // set di campioni letto dal thread audio
    juce::SpinLock padLock;
    std::unique_ptr<sf::PadSet> pads;
    std::unique_ptr<sf::PadSet> retired;   // liberato sul thread dei messaggi
    juce::AudioBuffer<float> original;     // brano intero per l'ascolto
    double originalRate = 44100;
    std::atomic<double> originalPos { -1.0 };

    // eventi dall'interfaccia al thread audio
    struct Event { int type; int pad; };   // 0 down, 1 up
    juce::AbstractFifo fifo { 256 };
    std::array<Event, 256> events;
    std::atomic<bool> panicRequested { false };
    double deviceRate = 44100;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleForgeProcessor)
};
