#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>

namespace
{
constexpr double maxSeconds = 300;   // si analizzano al massimo i primi 5 minuti

juce::String stemRole (juce::String name, const juce::String& prefix)
{
    name = name.upToLastOccurrenceOf (".", false, false);
    if (prefix.isNotEmpty() && name.startsWith (prefix) && name.length() > prefix.length()) name = name.substring (prefix.length());
    name = name.toLowerCase();
    auto any = [&] (std::initializer_list<const char*> keys) { for (auto k : keys) if (name.contains (k)) return true; return false; };
    if (any ({ "instrum", "karaoke", "no_voc", "no voc", "novoc", "minus", "backing" })) return "instrum";
    if (any ({ "drum", "batt", "perc", "beat" })) return "drums";
    if (any ({ "voc", "vox", "voce", "acap", "cappella", "sing" })) return "vocals";
    if (any ({ "bass", "basso" })) return "bass";
    if (any ({ "other", "altro", "chord", "music", "keys", "synth", "piano", "guitar", "chitarra", "strings", "wind", "accomp" })) return "other";
    return {};
}

juce::String commonPrefix (const juce::Array<juce::File>& files)
{
    if (files.size() < 2) return {};
    juce::String p = files[0].getFileNameWithoutExtension();
    for (auto& f : files) { auto n = f.getFileNameWithoutExtension(); while (p.isNotEmpty() && ! n.startsWith (p)) p = p.dropLastCharacters (1); }
    return p;
}

juce::String safeName (juce::String s)
{
    s = s.replace (juce::String::fromUTF8 ("\xe2\x99\xad"), "b");
    juce::String out;
    for (auto c : s)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (c) && c < 128) out += juce::String::charToString (c);
        else if (c == '#' || c == '+' || c == '-' || c == '.') out += juce::String::charToString (c);
        else if (! out.endsWithChar ('_')) out += "_";
    }
    out = out.trimCharactersAtStart ("_").trimCharactersAtEnd ("_").substring (0, 60);
    return out.isEmpty() ? "Brano" : out;
}

juce::String fmtBpm (double b) { return std::abs (b - std::round (b)) < 1e-9 ? juce::String ((int) std::round (b)) : juce::String (b, 2); }

sf::Stereo toStereo (const juce::AudioBuffer<float>& b, int len)
{
    sf::Stereo s;
    s.L.assign (b.getReadPointer (0), b.getReadPointer (0) + len);
    s.R.assign (b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0), b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0) + len);
    return s;
}
}  // namespace

SampleForgeProcessor::SampleForgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("Sample Forge analisi")
{
    formats.registerBasicFormats();

    ControllerLeds::Host h;
    h.padState = [this] (int p) { return padState (p); };
    h.currentBank = [this] { return uiBank.load(); };
    h.padDown = [this] (int p) { padDown (p); };
    h.padUp = [this] (int p) { padUp (p); };
    h.selectBank = [this] (int b) { uiBank = b; };
    leds = std::make_unique<ControllerLeds> (std::move (h));
    if (juce::SystemStats::getEnvironmentVariable ("SF_NO_LEDS", {}).isNotEmpty()) ledsWanted = false;
    setControllerLeds (ledsWanted);
}

void SampleForgeProcessor::setControllerLeds (bool on)
{
    ledsWanted = on;
    if (leds != nullptr) leds->setEnabled (on);
}

ControllerLeds::PadState SampleForgeProcessor::padState (int pad) const
{
    using S = ControllerLeds::PadState;
    if (pad < 0 || pad >= sf::numPads || result == nullptr) return S::empty;
    if (! result->banks[(size_t) (pad / sf::padsPerBank)].pads[(size_t) (pad % sf::padsPerBank)]) return S::empty;
    if (sampler.latched[(size_t) pad].load()) return S::latched;
    if (sampler.playhead[(size_t) pad].load() >= 0) return S::playing;
    return S::loaded;
}

SampleForgeProcessor::~SampleForgeProcessor()
{
    cancel = true;
    leds.reset();
    stopThread (10000);
    cancelPendingUpdate();
}

bool SampleForgeProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SampleForgeProcessor::prepareToPlay (double sampleRate, int)
{
    deviceRate = sampleRate;
    sampler.prepare (sampleRate);
}

void SampleForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const juce::SpinLock::ScopedTryLockType lock (padLock);
    const sf::PadSet* set = lock.isLocked() ? pads.get() : nullptr;

    if (panicRequested.exchange (false)) { sampler.panic(); originalPos = -1.0; }

    // eventi dall'interfaccia (tasti A-K, clic sui pad)
    {
        const auto scope = fifo.read (fifo.getNumReady());
        auto handle = [&] (int start, int n) { for (int i = 0; i < n; ++i) { const auto& e = events[(size_t) (start + i)]; if (e.type == 0) sampler.noteOn (set, e.pad); else sampler.noteOff (e.pad); } };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // MIDI: note 36-75 = banco 1 pad A ... banco 5 pad K; CC 120/123 = PANIC
    int pos = 0;
    const int n = buffer.getNumSamples();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { sampler.render (set, buffer, pos, at - pos); pos = at; }
        if ((m.isNoteOn() || m.isNoteOff()) && leds != nullptr && leds->claimedRecently (m.getNoteNumber())) continue;   // gia' gestita dal controller
        if (m.isNoteOn()) sampler.noteOn (set, m.getNoteNumber() - sf::firstMidiNote, m.getFloatVelocity());
        else if (m.isNoteOff()) sampler.noteOff (m.getNoteNumber() - sf::firstMidiNote);
        else if (m.isController() && (m.getControllerNumber() == 120 || m.getControllerNumber() == 123)) { sampler.panic(); originalPos = -1.0; }
    }
    if (pos < n) sampler.render (set, buffer, pos, n - pos);
    midi.clear();

    // ascolto del brano originale
    double op = originalPos.load();
    if (op >= 0 && lock.isLocked() && original.getNumSamples() > 0)
    {
        const double step = originalRate / deviceRate;
        const int len = original.getNumSamples();
        const float* L = original.getReadPointer (0); const float* R = original.getReadPointer (original.getNumChannels() > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            const int i0 = (int) op;
            if (i0 >= len - 1) { op = -1; break; }
            const float fr = (float) (op - i0);
            buffer.addSample (0, i, (L[i0] + (L[i0 + 1] - L[i0]) * fr) * 0.9f);
            buffer.addSample (1, i, (R[i0] + (R[i0 + 1] - R[i0]) * fr) * 0.9f);
            op += step;
        }
        originalPos = op;
    }

    // limitatore morbido d'uscita
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i) { const float x = d[i]; d[i] = std::abs (x) < 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((std::abs (x) - 0.8f) / 0.2f), x); }
    }
}

// ---------------------------------------------------------------- interfaccia -> audio
void SampleForgeProcessor::padDown (int pad)
{
    const juce::SpinLock::ScopedLockType sl (fifoWriteLock);
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) events[(size_t) scope.startIndex1] = { 0, pad };
    else if (scope.blockSize2 > 0) events[(size_t) scope.startIndex2] = { 0, pad };
}
void SampleForgeProcessor::padUp (int pad)
{
    const juce::SpinLock::ScopedLockType sl (fifoWriteLock);
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0) events[(size_t) scope.startIndex1] = { 1, pad };
    else if (scope.blockSize2 > 0) events[(size_t) scope.startIndex2] = { 1, pad };
}
void SampleForgeProcessor::playOriginal (double fromSeconds)
{
    originalPos = fromSeconds < 0 ? -1.0 : fromSeconds * originalRate;
}
double SampleForgeProcessor::originalPositionSeconds() const
{
    const double p = originalPos.load();
    return p < 0 ? -1 : p / originalRate;
}

// ---------------------------------------------------------------- lavori
SampleForgeProcessor::Status SampleForgeProcessor::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}
void SampleForgeProcessor::setStatus (bool busy, double p, const juce::String& msg)
{
    const juce::ScopedLock sl (statusLock);
    status.busy = busy; status.progress = p; status.message = msg;
}

void SampleForgeProcessor::loadFiles (const juce::Array<juce::File>& files)
{
    if (isThreadRunning()) { if (onToast) onToast (juce::String::fromUTF8 ("Attendi la fine dell'operazione in corso")); return; }
    pendingFiles = files; job = Job::analyze; cancel = false;
    setStatus (true, 0.01, "Decodifico l'audio");
    startThread (juce::Thread::Priority::normal);
}

void SampleForgeProcessor::reslice (double bpm)
{
    if (isThreadRunning() || result == nullptr) return;
    pendingBpm = bpm; job = Job::reslice; cancel = false;
    setStatus (true, 0.75, "Ricalcolo la griglia");
    startThread (juce::Thread::Priority::normal);
}

void SampleForgeProcessor::exportPack (const juce::File& zipFile, ExportOptions o)
{
    if (isThreadRunning() || result == nullptr) return;
    pendingZip = zipFile; pendingExport = o; job = Job::exportZip; cancel = false;
    setStatus (true, 0.0, "Preparo il pacchetto");
    startThread (juce::Thread::Priority::normal);
}

void SampleForgeProcessor::run()
{
    jobError.clear(); jobInfo.clear();
    try
    {
        if (job == Job::analyze) doAnalyze();
        else if (job == Job::reslice)
        {
            auto r = std::make_shared<sf::Result> (engine.reslice (pendingBpm, [this] (double p, const std::string& m) { setStatus (true, p, juce::String::fromUTF8 (m.c_str())); }, &cancel));
            pendingResult = r;
        }
        else if (job == Job::exportZip) doExport();
    }
    catch (const std::exception& e) { jobError = juce::String::fromUTF8 (e.what()); }
    triggerAsyncUpdate();
}

void SampleForgeProcessor::doAnalyze()
{
    juce::Array<juce::File> files = pendingFiles;
    const juce::String pre = commonPrefix (files);
    juce::StringArray roles;
    bool hasParts = false;
    for (auto& f : files) { auto r = files.size() > 1 ? stemRole (f.getFileName(), pre) : juce::String(); roles.add (r); hasParts |= (r == "drums" || r == "bass" || r == "other"); }
    for (auto& r : roles) if (r == "instrum") r = hasParts ? juce::String() : juce::String ("other");
    int recognised = 0; for (auto& r : roles) if (r.isNotEmpty()) recognised++;

    auto decode = [this] (const juce::File& f, double& rate) -> juce::AudioBuffer<float>
    {
        std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (f));
        if (rd == nullptr) throw std::runtime_error ((juce::String::fromUTF8 ("Formato non riconosciuto: ") + f.getFileName()).toStdString());
        rate = rd->sampleRate;
        const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (60 * 30 * rate));
        juce::AudioBuffer<float> b (2, len);
        rd->read (&b, 0, len, 0, true, true);
        if (rd->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, len);
        return b;
    };

    sf::Sources src;
    juce::AudioBuffer<float> full;
    double rate = 44100;
    stemMap.clear();
    juce::String name;
    if (files.size() > 1 && recognised >= 2)
    {
        src.stems = true;
        std::map<juce::String, juce::AudioBuffer<float>> bufs;
        for (int i = 0; i < files.size(); ++i)
        {
            if (roles[i].isEmpty()) continue;
            setStatus (true, 0.01, "Decodifico " + files[i].getFileName());
            double r = 0; auto b = decode (files[i], r);
            if (bufs.empty()) rate = r;
            else if (std::abs (r - rate) > 1) throw std::runtime_error ("Gli stem hanno frequenze di campionamento diverse.");
            stemMap.add (files[i].getFileName() + " > " + roles[i]);
            auto it = bufs.find (roles[i]);
            if (it == bufs.end()) bufs[roles[i]] = std::move (b);
            else
            {
                auto& a = it->second;
                const int len = std::max (a.getNumSamples(), b.getNumSamples());
                a.setSize (2, len, true, true);
                for (int c = 0; c < 2; ++c) a.addFrom (c, 0, b, c, 0, b.getNumSamples());
            }
        }
        int len = 0; for (auto& kv : bufs) len = std::max (len, kv.second.getNumSamples());
        full.setSize (2, len); full.clear();
        for (auto& kv : bufs) for (int c = 0; c < 2; ++c) full.addFrom (c, 0, kv.second, c, 0, kv.second.getNumSamples());
        const int cap = (int) std::min<double> (len, maxSeconds * rate);
        auto stem = [&] (const char* k) { auto it = bufs.find (k); if (it == bufs.end()) { sf::Stereo s; s.L.assign ((size_t) cap, 0); s.R.assign ((size_t) cap, 0); return s; }
                                          juce::AudioBuffer<float> b (2, cap); b.clear(); b.copyFrom (0, 0, it->second, 0, 0, std::min (cap, it->second.getNumSamples())); b.copyFrom (1, 0, it->second, 1, 0, std::min (cap, it->second.getNumSamples())); return toStereo (b, cap); };
        src.drums = stem ("drums"); src.vocals = stem ("vocals"); src.bass = stem ("bass"); src.other = stem ("other");
        name = pre.trimCharactersAtEnd (" _-.(");
        if (name.length() < 3) name = "Stem";
        if (bufs.find ("drums") == bufs.end()) jobInfo = juce::String::fromUTF8 ("Nessuno stem di batteria riconosciuto: il banco BATTERIA resterà vuoto");
    }
    else
    {
        full = decode (files[0], rate);
        name = files[0].getFileNameWithoutExtension();
        const int cap = (int) std::min<double> (full.getNumSamples(), maxSeconds * rate);
        src.mix = toStereo (full, cap);
    }
    const int capLen = (int) std::min<double> (full.getNumSamples(), maxSeconds * rate);
    const double capped = full.getNumSamples() > capLen ? capLen / rate : 0;

    // panoramica e brano per l'ascolto (sul thread dei messaggi dopo)
    std::vector<float> ov (2000, 0.f);
    {
        const int step = std::max (1, full.getNumSamples() / 2000); float mx = 1e-9f;
        for (int i = 0; i < 2000; ++i)
        {
            float m = 0;
            for (int j = i * step; j < (i + 1) * step && j < full.getNumSamples(); j += 8)
                m = std::max ({ m, std::abs (full.getSample (0, j)), std::abs (full.getSample (1, j)) });
            ov[(size_t) i] = m; mx = std::max (mx, m);
        }
        for (auto& v : ov) v /= mx;
    }

    auto r = std::make_shared<sf::Result> (engine.analyze (std::move (src), rate, capped,
        [this] (double p, const std::string& m) { setStatus (true, p, juce::String::fromUTF8 (m.c_str())); }, &cancel));

    {
        const juce::SpinLock::ScopedLockType sl (padLock);
        original = std::move (full); originalRate = rate; originalPos = -1.0;
    }
    pendingResult = r;
    pendingSongName = name;
    pendingOverview = std::move (ov);
}

void SampleForgeProcessor::installResult (std::shared_ptr<sf::Result> r)
{
    auto set = std::make_unique<sf::PadSet>();
    set->sampleRate = r->sr;
    for (int b = 0; b < sf::numBanks; ++b)
        for (int p = 0; p < sf::padsPerBank; ++p)
        {
            const auto& pad = r->banks[(size_t) b].pads[(size_t) p];
            auto& buf = set->buffers[(size_t) (b * sf::padsPerBank + p)];
            if (pad == nullptr) continue;
            const int len = (int) pad->audio.L.size();
            buf.setSize (2, len);
            buf.copyFrom (0, 0, pad->audio.L.data(), len);
            buf.copyFrom (1, 0, pad->audio.R.data(), len);
            set->loopable[(size_t) (b * sf::padsPerBank + p)] = pad->loop;
        }
    {
        const juce::SpinLock::ScopedLockType sl (padLock);
        retired = std::move (pads);
        pads = std::move (set);
    }
    sampler.panic();
    retired.reset();
    result = r;
}

void SampleForgeProcessor::handleAsyncUpdate()
{
    stopThread (1000);
    if (jobError.isNotEmpty())
    {
        setStatus (false, -1, "Errore: " + jobError);
        if (onToast) onToast (jobError);
    }
    else if (job == Job::analyze || job == Job::reslice)
    {
        if (job == Job::analyze) { songName = pendingSongName; overview = std::move (pendingOverview); songDuration = original.getNumSamples() / originalRate; }
        installResult (pendingResult);
        pendingResult.reset();
        int n = 0; for (auto& b : result->banks) for (auto& p : b.pads) if (p) n++;
        juce::String msg = juce::String (n) + " campioni pronti";
        if (result->cappedSec > 0) msg << juce::String::fromUTF8 (" · analizzati i primi ") << juce::roundToInt (result->cappedSec / 60) << " minuti";
        msg << (result->fromStems ? juce::String::fromUTF8 (" · dagli stem") : juce::String::fromUTF8 (" · separazione DSP approssimativa"));
        msg << ". Suona con A-K, cambia banco con 1-5.";
        setStatus (false, -1, msg);
        if (jobInfo.isNotEmpty() && onToast) onToast (jobInfo);
    }
    else if (job == Job::exportZip)
    {
        setStatus (false, -1, jobInfo);
        if (onToast) onToast (jobInfo);
    }
    job = Job::none;
    if (onStateChanged) onStateChanged();
}

// ---------------------------------------------------------------- export
void SampleForgeProcessor::doExport()
{
    auto r = result;
    if (r == nullptr) throw std::runtime_error ("Nessun brano analizzato.");
    const int bits = pendingExport.bits;
    const juce::String B0 = fmtBpm (r->bpm), key = safeName (juce::String::fromUTF8 (r->key.name.c_str())), song = safeName (songName);
    const juce::String root = "MBStudio91_SampleForge_" + song + "/";
    const char* dirs[sf::numBanks] = { "01_Batteria", "02_Voci", "03_Loop", "04_Accordi", "05_Basso" };

    struct Item { juce::String path; juce::AudioBuffer<float> audio; double rate; };
    std::vector<Item> items;
    juce::StringArray lines;
    for (int b = 0; b < sf::numBanks; ++b)
        for (int p = 0; p < sf::padsPerBank; ++p)
        {
            const auto& pad = r->banks[(size_t) b].pads[(size_t) p];
            if (pad == nullptr) continue;
            const juce::String label = juce::String::fromUTF8 (pad->label.c_str());
            juce::String nm = juce::String (p + 1).paddedLeft ('0', 2) + "_" + safeName (label);
            if (pad->kind == "loop" || (pad->kind == "vox" && pad->bars > 0)) nm << "_" << B0 << "bpm_" << key;
            else if (pad->kind == "chord") nm << "_" << B0 << "bpm";
            juce::AudioBuffer<float> a (2, (int) pad->audio.L.size());
            a.copyFrom (0, 0, pad->audio.L.data(), a.getNumSamples()); a.copyFrom (1, 0, pad->audio.R.data(), a.getNumSamples());
            items.push_back ({ juce::String (dirs[b]) + "/" + nm + ".wav", std::move (a), r->sr });
            lines.add (juce::String (dirs[b]) + "/" + nm + ".wav  -  " + label + ", " + juce::String (pad->dur, 3) + " s, preso a " + juce::String (pad->start, 2) + " s");
        }
    if (pendingExport.stems)
    {
        const char* names[5][2] = { { "drums", "Batteria" }, { "vocals", "Voce" }, { "bass", "Basso" }, { "chords", "Accordi" }, { "inst", "Strumentale" } };
        for (auto& nmp : names)
        {
            setStatus (true, 0.3, juce::String::fromUTF8 ("Ricostruisco lo stem ") + nmp[1]);
            auto s = engine.synthFull (nmp[0]);
            juce::AudioBuffer<float> a (2, (int) s.L.size());
            a.copyFrom (0, 0, s.L.data(), a.getNumSamples()); a.copyFrom (1, 0, s.R.data(), a.getNumSamples());
            items.push_back ({ juce::String ("07_Stem/Stem_") + nmp[1] + "_" + B0 + "bpm_" + key + ".wav", std::move (a), r->sr });
        }
    }
    if (pendingExport.copy432)
    {
        const size_t base = items.size();
        const juce::String b432 = fmtBpm (std::round (r->bpm * 432.0 / 440.0 * 100) / 100);
        for (size_t i = 0; i < base; ++i)
        {
            setStatus (true, 0.6 + 0.3 * i / base, "Converto a 432 Hz");
            const auto& it = items[i];
            const double ratio = 432.0 / 440.0;   // stessa durata x 440/432, intonazione -31,8 cent
            const int outLen = (int) std::ceil (it.audio.getNumSamples() / ratio);
            juce::AudioBuffer<float> o (2, outLen);
            for (int c = 0; c < 2; ++c)
            {
                juce::LagrangeInterpolator li;
                li.process (ratio, it.audio.getReadPointer (c), o.getWritePointer (c), outLen, it.audio.getNumSamples(), 0);
            }
            juce::String p = it.path;
            p = p.replace (B0 + "bpm", b432 + "bpm").replace (".wav", "_432Hz.wav");
            items.push_back ({ "432Hz/" + p, std::move (o), it.rate });
        }
    }

    juce::ZipFile::Builder zip;
    juce::WavAudioFormat wav;
    for (size_t i = 0; i < items.size(); ++i)
    {
        setStatus (true, 0.9 * i / items.size(), "Scrivo " + items[i].path.fromLastOccurrenceOf ("/", false, false));
        juce::MemoryBlock block;
        {
            std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::MemoryOutputStream> (block, false);
            auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (items[i].rate).withNumChannels (2).withBitsPerSample (bits));
            if (w == nullptr) throw std::runtime_error ("Impossibile scrivere il WAV.");
            w->writeFromAudioSampleBuffer (items[i].audio, 0, items[i].audio.getNumSamples());
        }
        items[i].audio.setSize (0, 0);
        zip.addEntry (std::make_unique<juce::MemoryInputStream> (block, true), 0, root + items[i].path, juce::Time::getCurrentTime());
    }
    juce::StringArray prog;
    for (auto& c : r->progression) { auto s = juce::String::fromUTF8 (c.c_str()); if (prog.isEmpty() || prog[prog.size() - 1] != s) prog.add (s); }
    juce::StringArray info;
    info.add ("SAMPLE FORGE - MBStudio91"); info.add ({});
    info.add ("Brano: " + songName);
    info.add ("BPM rilevato: " + B0);
    info.add (juce::String::fromUTF8 ("Tonalità stimata: ") + juce::String::fromUTF8 (r->key.name.c_str()) + " (" + juce::String::fromUTF8 (r->key.it.c_str()) + ")");
    info.add ("Progressione (per battuta, senza ripetizioni consecutive): " + prog.joinIntoString (" "));
    info.add (juce::String ("Sorgente: ") + (r->fromStems ? "stem separati caricati dall'utente" : "mix stereo, separazione DSP approssimativa (HPSS + estrazione del centro stereo)"));
    if (r->fromStems && ! stemMap.isEmpty()) info.add ("Stem usati: " + stemMap.joinIntoString ("; "));
    info.add ("Formato: WAV " + juce::String (bits) + " bit, " + juce::String ((int) r->sr) + " Hz, stereo, picchi normalizzati a -1 dBFS");
    if (pendingExport.copy432) info.add ("Cartella 432Hz: copie ricampionate (-31,8 cent, tempo x0,9818)");
    info.add ("Data: " + juce::Time::getCurrentTime().toString (true, true));
    info.add ({}); info.add ("CONTENUTO"); info.addArray (lines);
    info.add ({}); info.add ("Nota: usa i campioni solo da brani di cui possiedi i diritti o con licenza adeguata.");
    const juce::String infoText = info.joinIntoString ("\r\n");
    zip.addEntry (std::make_unique<juce::MemoryInputStream> (infoText.toRawUTF8(), infoText.getNumBytesAsUTF8(), true), 0, root + "INFO.txt", juce::Time::getCurrentTime());

    setStatus (true, 0.95, "Salvo lo zip");
    pendingZip.deleteFile();
    juce::FileOutputStream out (pendingZip);
    if (! out.openedOk()) throw std::runtime_error ("Impossibile scrivere il file zip.");
    double prog2 = 0;
    if (! zip.writeToStream (out, &prog2)) throw std::runtime_error ("Errore durante la scrittura dello zip.");
    out.flush();
    jobInfo = "Pacchetto salvato: " + pendingZip.getFileName() + " (" + juce::String ((int) items.size() + 1) + " file, "
              + juce::String (pendingZip.getSize() / 1048576.0, 1) + " MB)";
}

// ---------------------------------------------------------------- stato (impostazioni dei banchi)
void SampleForgeProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree t ("SampleForge");
    for (int b = 0; b < sf::numBanks; ++b)
    {
        auto& s = sampler.settings[(size_t) b];
        juce::ValueTree c ("Bank");
        c.setProperty ("vol", s.vol.load(), nullptr); c.setProperty ("pitch", s.pitch.load(), nullptr);
        c.setProperty ("a", s.attack.load(), nullptr); c.setProperty ("d", s.decay.load(), nullptr);
        c.setProperty ("s", s.sustain.load(), nullptr); c.setProperty ("r", s.release.load(), nullptr);
        c.setProperty ("mode", s.mode.load(), nullptr); c.setProperty ("choke", s.choke.load(), nullptr);
        t.appendChild (c, nullptr);
    }
    t.setProperty ("leds", ledsWanted, nullptr);
    juce::MemoryOutputStream mo (dest, false);
    t.writeToStream (mo);
}

void SampleForgeProcessor::setStateInformation (const void* data, int size)
{
    auto t = juce::ValueTree::readFromData (data, (size_t) size);
    if (! t.hasType ("SampleForge")) return;
    setControllerLeds ((bool) t.getProperty ("leds", ledsWanted));
    for (int b = 0; b < sf::numBanks && b < t.getNumChildren(); ++b)
    {
        auto c = t.getChild (b); auto& s = sampler.settings[(size_t) b];
        s.vol = (float) c.getProperty ("vol", s.vol.load()); s.pitch = (float) c.getProperty ("pitch", s.pitch.load());
        s.attack = (float) c.getProperty ("a", s.attack.load()); s.decay = (float) c.getProperty ("d", s.decay.load());
        s.sustain = (float) c.getProperty ("s", s.sustain.load()); s.release = (float) c.getProperty ("r", s.release.load());
        s.mode = (int) c.getProperty ("mode", s.mode.load()); s.choke = (bool) c.getProperty ("choke", s.choke.load());
    }
}

juce::AudioProcessorEditor* SampleForgeProcessor::createEditor() { return new SampleForgeEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SampleForgeProcessor(); }
