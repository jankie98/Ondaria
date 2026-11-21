/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Logica dell'arrangement: clip sulla linea del tempo delle tracce.

    Le posizioni sono in beat dall'inizio del progetto. Chi chiama apre la
    transazione di undo (una per gesto), come per il piano roll.
*/
namespace sonora::arrangement
{

/** Clip della linea del tempo di una traccia (esclusi gli slot della session). */
juce::Array<tracktion::Clip*> getClips (tracktion::AudioTrack&);

/** Battute in beat secondo il metro all'inizio del progetto. */
tracktion::BeatDuration barLength (tracktion::Edit&);

/** Crea una clip MIDI vuota a partire da start. */
tracktion::MidiClip* createMidiClip (tracktion::AudioTrack&, tracktion::BeatPosition start, int lengthInBars = 1);

/** Crea una clip da un file audio o MIDI a partire da start. */
tracktion::Clip* createClipFromFile (tracktion::AudioTrack&, tracktion::BeatPosition start, const juce::File&);

/** Sposta una clip (anche su un'altra traccia). L'inizio non va sotto zero. */
void moveClip (tracktion::Clip&, tracktion::BeatPosition newStart, tracktion::AudioTrack* newTrack = nullptr);

/** Cambia la lunghezza di una clip (minimo minLength). */
void resizeClip (tracktion::Clip&, tracktion::BeatDuration newLength, tracktion::BeatDuration minLength);

/** Duplica una clip subito dopo se stessa. */
tracktion::Clip* duplicateClip (tracktion::Clip&);

void deleteClip (tracktion::Clip&);

/** True se almeno una traccia sta suonando le clip della session invece dell'arrangement. */
bool isAnyTrackPlayingSession (tracktion::Edit&);

/** "Torna all'arrangement": ferma le clip della session e fa suonare la linea del tempo. */
void backToArrangement (tracktion::Edit&);

/** Fine dell'ultima clip, in beat (almeno minBars battute). */
tracktion::BeatPosition getEndOfContent (tracktion::Edit&, int minBars = 16);

} // namespace sonora::arrangement
