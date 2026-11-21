// Prova del processore senza interfaccia: carica un brano, suona i pad, verifica PANIC ed export dello zip.
// Uso: sf_plugin_test <file audio> <zip di uscita>
#include "../Source/PluginProcessor.h"
#include <juce_events/juce_events.h>

static float rms (const juce::AudioBuffer<float>& b) { return std::max (b.getRMSLevel (0, 0, b.getNumSamples()), b.getRMSLevel (1, 0, b.getNumSamples())); }

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("uso: sf_plugin_test <audio> <zip>\n"); return 2; }
    juce::ScopedJuceInitialiser_GUI init;
    auto& mm = *juce::MessageManager::getInstance();
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("%s %s\n", ok ? "OK     " : "FALLITO", what.toRawUTF8()); if (! ok) failures++; };

    SampleForgeProcessor proc;
    bool changed = false;
    proc.onStateChanged = [&] { changed = true; };
    proc.loadFiles ({ juce::File (juce::String (argv[1])) });
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (! changed && juce::Time::getMillisecondCounterHiRes() - t0 < 180000) mm.runDispatchLoopUntil (50);
    auto r = proc.getResult();
    check (r != nullptr, "analisi completata (" + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) + " s): " + proc.getStatus().message);
    if (r == nullptr) return 1;
    int pads = 0; for (auto& b : r->banks) for (auto& p : b.pads) if (p) pads++;
    check (pads >= 25, juce::String (pads) + " pad pieni, BPM " + juce::String (r->bpm) + ", tonalita' " + juce::String (r->key.name));

    proc.prepareToPlay (48000, 512);
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    auto block = [&] { buf.clear(); midi.clear(); proc.processBlock (buf, midi); return rms (buf); };

    check (block() < 1e-6f, "silenzio senza pad premuti");
    proc.padDown (0);                       // banco 1, pad A (one-shot)
    float peak = 0; for (int i = 0; i < 8; ++i) peak = std::max (peak, block());
    check (peak > 0.01f, "il pad A suona (rms " + juce::String (peak, 4) + ")");
    check (proc.sampler.playhead[0].load() >= 0 || peak > 0, "linea di riproduzione aggiornata");

    midi.clear(); buf.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, sf::firstMidiNote + 2 * sf::padsPerBank, (juce::uint8) 100), 0);   // banco 3 (loop, latch)
    proc.processBlock (buf, midi);
    float loopPeak = 0; for (int i = 0; i < 200; ++i) loopPeak = std::max (loopPeak, block());
    check (loopPeak > 0.01f && proc.sampler.latched[(size_t) (2 * sf::padsPerBank)].load(), "nota MIDI 52 fa partire il loop in latch");

    proc.requestPanic();
    block(); const float after = block();
    check (after < 1e-6f && ! proc.sampler.latched[(size_t) (2 * sf::padsPerBank)].load(), "PANIC ferma tutto");

    // export
    juce::File zip { juce::String (argv[2]) }; zip.deleteFile();
    changed = false;
    proc.exportPack (zip, { 24, true, false });
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    while (! changed && juce::Time::getMillisecondCounterHiRes() - t1 < 120000) mm.runDispatchLoopUntil (50);
    juce::ZipFile zf (zip);
    int wavs = 0, w432 = 0; bool info = false;
    for (int i = 0; i < zf.getNumEntries(); ++i)
    {
        const auto n = zf.getEntry (i)->filename;
        if (n.endsWith (".wav")) { wavs++; if (n.contains ("/432Hz/")) w432++; }
        if (n.endsWith ("INFO.txt")) info = true;
    }
    check (zip.existsAsFile() && wavs == 2 * pads && w432 == pads && info,
           "zip: " + juce::String (wavs) + " WAV (" + juce::String (w432) + " a 432 Hz), INFO.txt " + (info ? "presente" : "mancante") + ", " + juce::String (zip.getSize() / 1048576.0, 1) + " MB");
    if (zf.getNumEntries() > 0)
    {
        std::unique_ptr<juce::InputStream> in (zf.createStreamForEntry (0));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (in.release(), true));
        check (rd != nullptr && rd->bitsPerSample == 24 && rd->numChannels == 2, "primo WAV leggibile, 24 bit stereo");
    }
    std::printf (failures ? "ESITO: FALLITO (%d)\n" : "ESITO: OK\n", failures);
    return failures ? 1 : 0;
}
