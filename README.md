# Sample Forge · MBStudio91

Da un brano a un pacchetto di campioni da suonare e scaricare, tutto nel browser.

L'app separa il brano in batteria, voce, basso e accordi, ne trova BPM, tonalità e accordi e riempie cinque banchi da otto pad (batteria, voci, loop, accordi, basso). I pad si suonano con i tasti A–K, c'è un sequencer a 16 passi con registrazione quantizzata e l'export crea uno zip con i WAV (24 o 16 bit), il loop della sequenza e, se serve, una copia a 432 Hz.

L'interfaccia usa lo stile hardware MBStudio91 (telaio nero, serigrafia bianca, LED), lo stesso dei plugin VST.

## Versione nativa (senza browser)

Nella cartella [`native/`](native/) c'è Sample Forge riscritto in C++ con JUCE: app Standalone, VST3 e AU, con lo stesso stile hardware dei plugin MBStudio91. I file pronti per Mac, Windows e Linux escono dalla compilazione *Native (C++)* nella pagina **Releases**.

## Motori di separazione

- **Rapido, nel browser**: separazione DSP approssimativa (maschere di Wiener a 4 sorgenti, coerenza stereo, HPSS a due scale). Il brano non lascia il computer.
- **MVSep automatico**: con una chiave API di [mvsep.com](https://mvsep.com) l'app invia il brano, aspetta il risultato e scarica gli stem. La chiave resta salvata solo nel browser di chi la usa: non va mai scritta nei file del repository.
- **Stem caricati a mano**: trascina insieme gli stem già separati (MVSep, MVSEP-MDX23, Demucs, UVR); vengono riconosciuti dal nome.

## Scaricare

Ogni push su `main` fa partire la compilazione su GitHub Actions. I file pronti stanno negli artifact dell'ultima esecuzione (scheda **Actions** › ultima esecuzione › **sample-forge**):

| File | Cos'è |
|---|---|
| `sample-forge.html` | l'app completa in un solo file, si apre con un doppio clic |
| `sample-pack-app.html` | la stessa app col nome Sample Pack Creator, per il sito MBStudio91 |
| `SampleForge-Mac.zip` | app per Mac: decomprimi e trascina *Sample Forge.app* in Applicazioni; al primo avvio clic destro › Apri |
| `screenshot.png` | schermata della prova automatica |

Con un tag `v*` (per esempio `v1.2`) gli stessi file finiscono anche in una **Release**.

## Struttura

```
src/template.html   interfaccia, audio, sequencer, export (il CSS hardware MBStudio è incorporato)
src/worker.js       motore DSP: separazione, BPM, tonalità, accordi, ritaglio dei campioni
mac/                Info.plist, avvio e icona dell'app per Mac
build.py            unisce i sorgenti e prepara dist/
tests/              prova nel browser e banco di prova della separazione (SDR)
licenses/           licenze dei caratteri
```

In locale: `python3 build.py`, poi apri `dist/sample-forge.html`. Per le prove: `npm install && npx playwright install chromium && npm test`; per misurare la separazione: `npm run bench`.

## Licenze e uso

Programma di MBStudio91 per uso personale. Usa solo brani di cui hai i diritti o con licenza adeguata.

Caratteri Bebas Neue (© Dharma Type) e Antonio (© Vernon Adams), SIL Open Font License 1.1: testi in `licenses/`. Il logo MBSTUDIO91 è un marchio di MBStudio91.
