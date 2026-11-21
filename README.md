# Sonora

Sonora è una DAW open source per **Linux e Windows**, pensata per un flusso di lavoro basato su clip, scene e device, con un host di plugin che carica tutti i formati principali.

Il progetto è un'implementazione originale (codice, nome e grafica) e non contiene codice o risorse di altre DAW commerciali.

> Stato: **prima versione in sviluppo** (passo 2 della roadmap). L'app apre un progetto, carica plugin di ogni formato su più tracce e li fa suonare con la tastiera virtuale. Session view e arrangement arrivano nel passo 3.

## Cosa c'è già

- **Motore audio** basato su [Tracktion Engine](https://github.com/Tracktion/tracktion_engine) 3.5 e [JUCE](https://juce.com) 9.
- **Host dei plugin multi-formato:**

  | Formato | Linux | Windows | Implementazione |
  | --- | --- | --- | --- |
  | VST3 | sì | sì | JUCE |
  | CLAP | sì | sì | host nostro (`src/host/ClapPluginFormat.cpp`) |
  | LV2 | sì | sì | JUCE |
  | LADSPA | sì | no | JUCE |
  | VST2 | opzionale | opzionale | JUCE, solo se compilato con l'SDK VST2 (`-DSONORA_VST2_SDK_DIR=...`) |

- **Scansione isolata:** ogni file di plugin viene aperto da `sonora-plugin-scanner`, un processo separato. Se un plugin va in crash o si blocca, finisce nella lista di esclusione e la DAW resta aperta.
- **Browser dei device** con ricerca, strumenti ed effetti integrati e tutti i plugin trovati.
- **Catena di device** per traccia: aggiungi, apri l'interfaccia, attiva/disattiva, rimuovi.
- **Tastiera MIDI virtuale** per suonare gli strumenti anche senza controller.

## Compilare

Requisiti: CMake 3.22+, un compilatore C++20 (GCC 11+, Clang 14+ o Visual Studio 2022).

```bash
git clone --recurse-submodules <url-del-repository> sonora
cd sonora
```

**Linux (Ubuntu/Debian):**

```bash
sudo apt install ninja-build libasound2-dev libjack-jackd2-dev ladspa-sdk libfreetype-dev \
  libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev mesa-common-dev libgl-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/Sonora_artefacts/Release/Sonora
```

**Windows (Visual Studio 2022):**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Sonora_artefacts\Release\Sonora.exe
```

**Test:** `ctest --test-dir build -C Release --output-on-failure`. I test caricano due plugin CLAP di prova (uno valido e uno che va in crash) e verificano parametri, audio, note, stato, latenza e scansione isolata.

## Struttura

```
src/app/       interfaccia: finestra principale, browser, catena di device, finestre dei plugin
src/host/      host dei plugin: formato CLAP, registro dei formati, scansione isolata
src/scanner/   sonora-plugin-scanner, il processo che scansiona un plugin alla volta
tests/         test automatici e plugin CLAP di prova
external/      sottomoduli: Tracktion Engine (con JUCE) e SDK CLAP
docs/          architettura e roadmap
```

## Licenza

AGPL-3.0-or-later (vedi `LICENSE`). JUCE è distribuito con licenza AGPLv3 e Tracktion Engine con GPLv3 o successiva, entrambe compatibili.
