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

## Fase 2

- **Sequenza**: tasto SEQUENZA sopra i pad. La griglia mostra gli 8 pad del banco scelto per 16 passi; BATT. 1-4 scelgono la battuta. Clic accende o spegne il passo, trascinando si dipinge la riga. MUTE BANCO zittisce il banco nella sequenza, PULISCI BANCO lo svuota (con SHIFT, per non farlo per sbaglio). Si salva col progetto della DAW.
- **PLAY, REC, METRO** nella riga TEMPO, RITAGLIO E SEQUENZA. BATTUTE 1, 2 o 4; SWING da 0 a 100 (ritarda i passi pari fino a un terzo di passo, come nella versione web). Il tempo è il BPM del brano, oppure quello scritto nel campo BPM. In una DAW che suona, la sequenza segue tempo e posizione del progetto.
- **REC**: con la sequenza in moto, i pad suonati (tasti A-K, clic, MIDI o controller) finiscono sul passo più vicino, quantizzati a 1/16, 1/8 o 1/4 (QUANT REC). Se il passo non è ancora suonato, il sequencer non lo ribatte.
- **Export**: con LOOP DELLA SEQUENZA acceso lo zip contiene `06_Sequenza/Sequenza_<battute>batt_<bpm>bpm_<tonalità>.wav`, lungo esattamente le battute: la coda dei suoni oltre la fine si somma all'inizio, così il loop gira senza buchi. Con COPIA A 432 HZ c'è anche la sua copia.
- **MVSEP**: il tasto apre la finestra per la chiave API, il modello (BS Roformer SW 6 stem, Ensemble 5 stem, Demucs4 HT 4 stem) e il formato degli stem. Con USA MVSEP, caricando un brano solo l'app lo invia a mvsep.com, aspetta, scarica gli stem e li analizza. La chiave resta in un file di impostazioni sul computer (cartella MBStudio91 in Application Support), mai nei file del repository. Serve internet; nella versione Linux la rete non è attiva.
- **BRANO DI PROVA**: genera e analizza un brano sintetico (112 BPM, La minore, Am-F-C-G), lo stesso della versione web.

## Compilare

Lo fa GitHub Actions a ogni push (`.github/workflows/native.yml`): i file pronti sono negli artifact e nella pagina Releases.

In locale: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release` (JUCE 9.0.5 viene scaricato da solo, oppure `-DJUCE_DIR=...`).

## Prove

- `sf_engine_cli <cartella>`: motore da solo sui brani di prova con stem noti (`node tests/dump_songs.mjs testdata`), stampa BPM, tonalità e SDR di ogni stem.
- `SampleForgePluginTest <wav> <zip>`: processore senza interfaccia (pad, MIDI, PANIC, export).
- Standalone con `SF_AUTOLOAD=<wav> SF_SCREENSHOT=<png>`: carica, analizza, salva una schermata ed esce.

Caratteri Bebas Neue e Antonio (SIL Open Font License 1.1) in `MBStudioUI/fonts`.
