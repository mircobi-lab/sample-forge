// Sample Forge · MBStudio91 — separazione con l'API di MVSep (mvsep.com), come nella versione web:
// invia il brano (POST /api/separation/create), aspetta il risultato (GET /api/separation/get?hash=...)
// e scarica gli stem. La chiave resta in un file di impostazioni sul computer, mai nel repository.
// Riferimento API: https://mvsep.com/full_api (campi api_token, audiofile, sep_type, output_format, is_demo).
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <functional>
#include <stdexcept>

namespace sf
{
struct MvsepChoice { int value; const char* label; };
// stessi modelli e formati della versione web (src/template.html)
inline const MvsepChoice mvsepModels[] = { { 63, "BS ROFORMER SW - 6 STEM" }, { 28, "ENSEMBLE - 5 STEM" }, { 20, "DEMUCS4 HT - 4 STEM" } };
inline const MvsepChoice mvsepFormats[] = { { 5, "FLAC 24 BIT" }, { 2, "FLAC 16 BIT" }, { 1, "WAV 16 BIT" } };

struct MvsepSettings
{
    juce::String key;
    int model = 63, format = 5;
    bool enabled = false;

    static juce::PropertiesFile::Options options()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Sample Forge"; o.folderName = "MBStudio91";
        o.filenameSuffix = ".settings"; o.osxLibrarySubFolder = "Application Support";
        return o;
    }
    void load()
    {
        juce::PropertiesFile p (options());
        key = p.getValue ("mvsepKey"); model = p.getIntValue ("mvsepModel", 63); format = p.getIntValue ("mvsepFormat", 5);
        enabled = p.getBoolValue ("mvsepEnabled", false);
    }
    void save() const
    {
        juce::PropertiesFile p (options());
        p.setValue ("mvsepKey", key); p.setValue ("mvsepModel", model); p.setValue ("mvsepFormat", format); p.setValue ("mvsepEnabled", enabled);
        p.saveIfNeeded();
    }
};

/** Esegue la separazione completa sul thread chiamante. Restituisce gli stem scaricati in outDir.
    status(progresso 0..1, messaggio). cancel: interrompe l'attesa. Lancia std::runtime_error con messaggi in italiano. */
inline juce::Array<juce::File> mvsepSeparate (const juce::File& song, const MvsepSettings& s, const juce::File& outDir,
                                              std::function<void (double, const juce::String&)> status, std::atomic<bool>* cancel)
{
    const juce::String api = "https://mvsep.com/api";
    auto fail = [] (const juce::String& m) { throw std::runtime_error (("MVSep: " + m).toStdString()); };
    if (s.key.trim().isEmpty()) fail ("manca la chiave API. Aprila con il tasto MVSEP e incollala.");

    status (.02, "Invio il brano a MVSep (" + juce::String (song.getSize() / 1048576.0, 1) + " MB)");
    int code = 0;
    juce::String hash;
    {
        auto url = juce::URL (api + "/separation/create")
                       .withParameter ("api_token", s.key.trim())
                       .withParameter ("sep_type", juce::String (s.model))
                       .withParameter ("output_format", juce::String (s.format))
                       .withParameter ("is_demo", "0")
                       .withFileToUpload ("audiofile", song, "application/octet-stream");
        auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                             .withConnectionTimeoutMs (10 * 60 * 1000).withStatusCode (&code));
        if (in == nullptr) fail ("non riesco a collegarmi (rete assente o connessione bloccata).");
        const auto j = juce::JSON::parse (in->readEntireStreamAsString());
        if (code == 401) fail ("chiave API non valida o scaduta. Incolla quella nuova con il tasto MVSEP.");
        if (! (bool) j.getProperty ("success", false))
        {
            const auto msg = j.getProperty ("data", {}).getProperty ("message", {}).toString();
            fail (msg.isNotEmpty() ? msg : "richiesta rifiutata (HTTP " + juce::String (code) + ").");
        }
        hash = j.getProperty ("data", {}).getProperty ("hash", {}).toString();
        if (hash.isEmpty()) fail ("risposta senza codice del lavoro.");
    }

    juce::var files;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (;;)
    {
        for (int i = 0; i < 50; ++i) { if (cancel != nullptr && cancel->load()) fail ("annullato."); juce::Thread::sleep (100); }
        const double el = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        if (el > 45 * 60) fail ("tempo scaduto in attesa del risultato. Riprova più tardi.");
        auto in = juce::URL (api + "/separation/get").withParameter ("hash", hash)
                      .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (30000));
        if (in == nullptr) continue;
        const auto j = juce::JSON::parse (in->readEntireStreamAsString());
        const auto st = j.getProperty ("status", {}).toString();
        const auto d = j.getProperty ("data", {});
        if (st == "done") { files = d.getProperty ("files", {}); break; }
        if (st == "failed" || st == "not_found")
        {
            const auto m = d.getProperty ("message", {}).toString();
            fail ("separazione non riuscita" + (m.isNotEmpty() ? ": " + m : juce::String (".")));
        }
        juce::String what = st == "waiting" ? "in coda" : st == "merging" ? "unisco i risultati" : "in elaborazione";
        if (st == "waiting" && ! d.getProperty ("current_order", {}).isVoid())
            what << " (posizione " << d.getProperty ("current_order", {}).toString() << ")";
        const int sec = (int) el;
        status (st == "waiting" ? .08 : st == "merging" ? .5 : .3,
                "MVSep: " + what + " - " + juce::String (sec / 60) + ":" + juce::String (sec % 60).paddedLeft ('0', 2));
    }
    if (! files.isArray() || files.size() == 0) fail ("nessun file nel risultato.");

    outDir.createDirectory();
    juce::Array<juce::File> out;
    for (int i = 0; i < files.size(); ++i)
    {
        const auto f = files[i];
        const auto u = f.getProperty ("url", {}).toString().replace ("\\/", "/");
        if (u.isEmpty()) continue;
        juce::String nm = f.getProperty ("download", {}).toString();
        if (nm.isEmpty()) nm = f.getProperty ("name", {}).toString();
        if (nm.isEmpty()) nm = juce::URL::removeEscapeChars (u.fromLastOccurrenceOf ("/", false, false).upToFirstOccurrenceOf ("?", false, false));
        nm = juce::File::createLegalFileName (nm);
        status (.55 + .4 * i / files.size(), "Scarico gli stem (" + juce::String (i + 1) + "/" + juce::String (files.size()) + "): " + nm);
        auto in = juce::URL (u).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (60000));
        if (in == nullptr) fail ("non riesco a scaricare " + nm + ".");
        const auto dest = outDir.getChildFile (nm);
        dest.deleteFile();
        juce::FileOutputStream fo (dest);
        if (! fo.openedOk()) fail ("non riesco a scrivere " + dest.getFullPathName() + ".");
        fo.writeFromInputStream (*in, -1);
        fo.flush();
        out.add (dest);
    }
    return out;
}
}  // namespace sf
