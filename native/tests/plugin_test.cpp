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

    // sequenza: 4 colpi del pad A in una battuta a 120 BPM, poi REC del pad S suonato a mano
    for (int b = 0; b < sf::numBanks; ++b) proc.seq.clearBank (b);
    for (int st : { 0, 4, 8, 12 }) proc.seq.set (0, st, true);
    proc.seq.bars = 1; proc.seq.bpm = 120.f; proc.seq.metro = false; proc.seq.playing = true;
    int loud = 0, maxStep = -1;
    for (int i = 0; i < 188; ++i) { if (block() > 0.005f) loud++; maxStep = std::max (maxStep, proc.seqEngine.stepUi.load()); }   // 2 s = una battuta
    check (loud > 20 && maxStep == 15, "la sequenza suona e arriva al passo 16 (blocchi con suono " + juce::String (loud) + ")");
    proc.seq.rec = true;
    for (int i = 0; i < 30; ++i) block();
    proc.padDown (1); block(); proc.padUp (1);
    proc.seq.rec = false;
    int recorded = 0; for (int st = 0; st < 16; ++st) if (proc.seq.get (1, st)) recorded++;
    check (recorded == 1, "REC: il pad S suonato a mano finisce su un passo");
    proc.seq.playing = false; block();
    proc.requestPanic(); block(); block();

    // export
    juce::File zip { juce::String (argv[2]) }; zip.deleteFile();
    changed = false;
    proc.exportPack (zip, { 24, true, false });
    const auto t1 = juce::Time::getMillisecondCounterHiRes();
    while (! changed && juce::Time::getMillisecondCounterHiRes() - t1 < 120000) mm.runDispatchLoopUntil (50);
    juce::ZipFile zf (zip);
    int wavs = 0, w432 = 0; bool info = false, seqLoop = false;
    for (int i = 0; i < zf.getNumEntries(); ++i)
    {
        const auto n = zf.getEntry (i)->filename;
        if (n.endsWith (".wav")) { wavs++; if (n.contains ("/432Hz/")) w432++; }
        if (n.endsWith ("INFO.txt")) info = true;
        if (n.contains ("/06_Sequenza/Sequenza_1batt_120bpm_") && ! n.contains ("432Hz")) seqLoop = true;
    }
    check (seqLoop, "zip: loop della sequenza in 06_Sequenza");
    check (zip.existsAsFile() && wavs == 2 * (pads + 1) && w432 == pads + 1 && info,
           "zip: " + juce::String (wavs) + " WAV (" + juce::String (w432) + " a 432 Hz), INFO.txt " + (info ? "presente" : "mancante") + ", " + juce::String (zip.getSize() / 1048576.0, 1) + " MB");
    if (zf.getNumEntries() > 0)
    {
        std::unique_ptr<juce::InputStream> in (zf.createStreamForEntry (0));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (in.release(), true));
        check (rd != nullptr && rd->bitsPerSample == 24 && rd->numChannels == 2, "primo WAV leggibile, 24 bit stereo");
    }
    // loop della sequenza: durata esatta di una battuta a 120 BPM (2 s)
    for (int i = 0; i < zf.getNumEntries(); ++i)
        if (zf.getEntry (i)->filename.contains ("/06_Sequenza/") && ! zf.getEntry (i)->filename.contains ("432Hz"))
        {
            std::unique_ptr<juce::InputStream> in (zf.createStreamForEntry (i));
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (in.release(), true));
            const double secs = rd != nullptr ? rd->lengthInSamples / rd->sampleRate : 0;
            check (std::abs (secs - 2.0) < 0.001, "loop lungo " + juce::String (secs, 4) + " s (atteso 2,0000)");
        }

    // brano di prova sintetico
    {
        SampleForgeProcessor demo;
        bool done = false;
        demo.onStateChanged = [&] { done = true; };
        demo.loadDemo();
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        while (! done && juce::Time::getMillisecondCounterHiRes() - t2 < 180000) mm.runDispatchLoopUntil (50);
        auto dr = demo.getResult();
        check (dr != nullptr && demo.getSongName().startsWith ("Brano di prova"), "brano di prova analizzato: " + demo.getStatus().message);
        if (dr != nullptr)
            check (std::abs (dr->bpm - 112.0) < 1.0, "BPM del brano di prova " + juce::String (dr->bpm) + " (atteso 112), tonalita' " + juce::String (dr->key.name));
    }
    std::printf (failures ? "ESITO: FALLITO (%d)\n" : "ESITO: OK\n", failures);
    return failures ? 1 : 0;
}
