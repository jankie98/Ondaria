# Architettura

Il documento completo con la scelta della base, i progetti valutati e la roadmap è nel documento di progetto (passo 1). Qui c'è la sintesi utile per chi legge il codice.

## Livelli

1. **Interfaccia** (`src/app`): componenti JUCE. Non tocca il motore audio direttamente.
2. **Servizi applicativi**: comandi, undo, salvataggio (oggi gestiti da Tracktion `Edit`; verranno separati nel passo 3).
3. **Motore**: Tracktion Engine (modello `Edit`, grafo audio multi-thread, clip e scene, time-stretch).
4. **Host dei plugin** (`src/host`): formati JUCE + formato CLAP nostro, scansione isolata.
5. **Driver**: JUCE (ALSA, JACK, PipeWire tramite JACK, ASIO, WASAPI, MIDI).

## Host CLAP

`ClapPluginFormat` implementa `juce::AudioPluginFormat`, quindi Tracktion Engine, la scansione e il browser trattano i plugin CLAP come gli altri formati.

- Un file `.clap` può contenere più plugin: `fileOrIdentifier` è il percorso del file, `uniqueId` è l'hash dell'id CLAP.
- Estensioni del plugin usate: audio-ports, note-ports, params, state, latency, tail, gui, timer-support, posix-fd-support.
- Estensioni offerte dall'host: log, thread-check, params, state, latency, audio-ports, note-ports, tail, gui, timer-support, posix-fd-support (Linux).
- Thread audio senza allocazioni: eventi preallocati, parametri passati con flag atomici, notifiche all'interfaccia da un timer a 30 Hz sul message thread.
- Interfaccia grafica: la finestra del plugin è figlia della finestra nativa che contiene l'editor (X11 su Linux, Win32 su Windows).

## Scansione isolata

`IsolatedPluginScanner` sostituisce lo scanner di `KnownPluginList`. Per ogni file avvia `sonora-plugin-scanner --format=... --file=... --output=...` con un timeout di 60 secondi. Crash, timeout o codice di uscita diverso da 0 mettono il file nella lista di esclusione.

## Prossimi passi dell'host

- Plugin in processi separati anche durante la riproduzione (memoria condivisa per l'audio), scelti per singolo plugin.
- Modulazione e note expression CLAP per voce.
- Scala dell'interfaccia dei plugin su schermi HiDPI.
