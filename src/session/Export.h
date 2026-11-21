/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Export: il brano (o il loop) diventa un file audio, tutto insieme o una
    traccia per file (stem). Il render gira in sottofondo, senza fermare l'interfaccia.
*/
namespace sonora::exporting
{

enum class Format { wav, flac, mp3, ogg };

/** Quale parte del brano esportare. */
enum class Range { song, loop };

struct Settings
{
    Format format = Format::wav;
    Range range = Range::song;
    double sampleRate = 44100.0;
    int bitDepth = 24;              ///< WAV: 16, 24 o 32 (virgola mobile). FLAC: 16 o 24.
    int mp3Kbps = 320;              ///< MP3 a bitrate costante
    bool stems = false;             ///< un file per traccia invece del mix
    bool normalise = false;         ///< porta il picco a -0.3 dB
    bool includeTails = true;       ///< lascia finire riverberi e delay dopo la fine

    /** Il file per il mix, la cartella per gli stem. */
    juce::File destination;
};

juce::String extensionFor (Format);
juce::String nameFor (Format);

/** L'MP3 usa la libreria LAME del sistema (pacchetto "lame" su Arch,
    libmp3lame.dll accanto al programma su Windows): c'e'? */
bool isAvailable (tracktion::Engine&, Format);

/** Profondita' possibili per il formato (vuota se il formato decide da solo). */
juce::Array<int> bitDepthsFor (Format);

/** Il tratto da esportare: dall'inizio alla fine dell'ultima clip, o il loop. */
std::optional<tracktion::TimeRange> timeRangeFor (tracktion::Edit&, Range);

/** Dove salvare se non si sceglie: accanto al progetto, col suo nome. */
juce::File defaultDestination (tracktion::Edit&, const Settings&);

/** I lavori di render per le impostazioni date (uno per il mix, uno per traccia per gli stem). */
juce::Result planJobs (tracktion::Edit&, const Settings&, std::vector<tracktion::PlannedRenderJob>& jobs);

//==============================================================================
/** Esegue un export in sottofondo. Tutto dal thread dei messaggi. */
class Exporter
{
public:
    Exporter() = default;
    ~Exporter();

    /** Parte subito; fallisce se le impostazioni non vanno bene. */
    juce::Result start (tracktion::Edit&, const Settings&);

    bool isRunning() const;
    float getProgress() const;

    /** Nome del lavoro in corso ("Mix", o il nome della traccia). */
    juce::String getCurrentJobName() const;

    void cancel();

    /** Alla fine: risultato e file scritti. */
    std::function<void (juce::Result, juce::Array<juce::File>)> onFinished;

private:
    std::unique_ptr<tracktion::RenderQueue> queue;
    juce::String current;
};

} // namespace sonora::exporting
