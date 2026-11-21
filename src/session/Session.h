/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Logica della session view, separata dall'interfaccia per poterla testare.

    La griglia e' fatta di tracce audio (colonne) e scene (righe): ogni traccia
    ha uno slot per scena e ogni slot puo' contenere una clip. Le clip si
    lanciano rispettando la quantizzazione globale del progetto, come in un
    clip launcher: lanciare una clip ferma le altre della stessa traccia.

    Le funzioni che modificano il progetto aprono una nuova transazione di
    undo, cosi' ogni azione si annulla con un solo Ctrl+Z.
*/
namespace sonora::session
{

/** Numero di scene di un progetto nuovo. */
inline constexpr int defaultNumScenes = 8;

/** Colori delle tracce e delle clip: 16 tinte di media saturazione. */
juce::Colour colourForIndex (int index);

/** Allinea la griglia: almeno minScenes scene e uno slot per scena su ogni traccia. */
void ensureGrid (tracktion::Edit&, int minScenes = defaultNumScenes);

/** Aggiunge una traccia audio/MIDI in fondo, con colore e slot. */
tracktion::AudioTrack* addTrack (tracktion::Edit&);

/** Aggiunge una scena in fondo e uno slot su ogni traccia. */
tracktion::Scene* addScene (tracktion::Edit&);

/** Elimina una traccia (con tutte le sue clip). */
void deleteTrack (tracktion::AudioTrack&);

/** Restituisce lo slot di una traccia per una scena, o nullptr. */
tracktion::ClipSlot* getSlot (tracktion::AudioTrack&, int sceneIndex);

/** Crea una clip MIDI vuota in loop, lunga lengthInBars battute. */
tracktion::MidiClip* createMidiClip (tracktion::ClipSlot&, int lengthInBars = 1);

/** Crea una clip da un file audio o MIDI. Restituisce nullptr se il tipo non e' supportato. */
tracktion::Clip* createClipFromFile (tracktion::ClipSlot&, const juce::File&);

/** Elimina la clip contenuta nello slot. */
void deleteClip (tracktion::ClipSlot&);

/** Lancia una clip: le altre clip della traccia si fermano al prossimo punto di quantizzazione. */
void launchClip (tracktion::Clip&);

/** Lancia tutte le clip di una scena. Le tracce con lo slot vuoto continuano a suonare. */
void launchScene (tracktion::Edit&, int sceneIndex);

/** Ferma le clip di una traccia. */
void stopTrack (tracktion::AudioTrack&);

/** Ferma tutte le clip del progetto. */
void stopAllClips (tracktion::Edit&);

/** Stato di uno slot, per disegnarlo. */
enum class SlotState
{
    empty,          ///< nessuna clip
    stopped,        ///< clip ferma
    playQueued,     ///< partira' al prossimo punto di quantizzazione
    playing,        ///< in riproduzione
    stopQueued      ///< si fermera' al prossimo punto di quantizzazione
};

SlotState getSlotState (tracktion::ClipSlot&);

/** Avanzamento (0..1) della clip in riproduzione, se sta suonando. */
std::optional<float> getPlayProgress (tracktion::Clip&);

/** Il prossimo punto di quantizzazione del lancio, sia come beat monotono (per le
    clip) sia come tempo del progetto senza loop (per le registrazioni).
    Vuoto se il trasporto e' fermo. */
struct LaunchPoint
{
    tracktion::MonotonicBeat beat;
    tracktion::TimePosition unloopedTime;
};

std::optional<LaunchPoint> nextLaunchPoint (tracktion::Edit&);

/** Fa partire il trasporto dall'inizio della battuta corrente, se e' fermo. */
void startTransportFromBar (tracktion::Edit&);

/** I percorsi relativi dei file (campioni importati, registrazioni) sono scritti
    rispetto al file del progetto: questa funzione li fa ritrovare nello stesso
    modo. Da chiamare subito dopo aver creato o aperto un Edit. */
void setUpFilePaths (tracktion::Edit&);

/** Apre un progetto salvato. A differenza di tracktion::loadEditFromFile tiene
    l'identificativo scritto nel file, cosi' il progetto ritrova la sua cartella
    temporanea (e i file delle tracce congelate). Ritorna nullptr se il file non si legge. */
std::unique_ptr<tracktion::Edit> loadEdit (tracktion::Engine&, const juce::File&);

/** Nomi dei valori di quantizzazione del lancio, nell'ordine di LaunchQType. */
juce::StringArray getQuantisationNames();

} // namespace sonora::session
