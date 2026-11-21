# Sample Forge nativo (C++ / JUCE)

La stessa app di `../src`, riscritta in C++ con JUCE e il kit grafico MBStudioUI dei plugin MBStudio91. Non usa Chrome né altri browser.

Formati: app **Standalone**, **VST3** e **AU** (su Mac). Il plugin è uno strumento: nella DAW le note MIDI 36–75 (C1–D#4) suonano i 40 pad, banco dopo banco; CC 120/123 e il tasto PANIC fermano tutto.

## Cosa c'è (versione 2.0, fase 1)

- motore DSP portato dal JavaScript, senza dipendenze (`Source/Engine`): separazione approssimativa a 4 sorgenti, BPM, tonalità, accordi, ritaglio dei campioni; stessi risultati della versione web, circa 4 volte più veloce
- caricamento di un mix o degli stem (riconosciuti dal nome come nella versione web), primi 5 minuti
- 5 banchi × 8 pad con ADSR, intonazione, one-shot / gate / latch, choke, linea di riproduzione
- tasti A–K, 1–5, spazio; ascolto del brano originale cliccando sulla forma d'onda
- nuovo ritaglio con BPM imposto (/2, x2)
- export zip: WAV 24/16 bit, copia a 432 Hz, stem completi, INFO.txt

In arrivo (fase 2): sequencer a 16 passi con registrazione, export del loop della sequenza, motore MVSep con chiave API, brano di prova.

## Compilare

Lo fa GitHub Actions a ogni push (`.github/workflows/native.yml`): i file pronti sono negli artifact e nella pagina Releases.

In locale: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release` (JUCE 9.0.5 viene scaricato da solo, oppure `-DJUCE_DIR=...`).

## Prove

- `sf_engine_cli <cartella>`: motore da solo sui brani di prova con stem noti (`node tests/dump_songs.mjs testdata`), stampa BPM, tonalità e SDR di ogni stem.
- `SampleForgePluginTest <wav> <zip>`: processore senza interfaccia (pad, MIDI, PANIC, export).
- Standalone con `SF_AUTOLOAD=<wav> SF_SCREENSHOT=<png>`: carica, analizza, salva una schermata ed esce.

Caratteri Bebas Neue e Antonio (SIL Open Font License 1.1) in `MBStudioUI/fonts`.
