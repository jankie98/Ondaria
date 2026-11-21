/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Modifica delle note di una clip MIDI, usata dal piano roll.

    Le posizioni sono in beat dall'inizio della clip. Ogni funzione che
    modifica usa l'undo manager del progetto: chi chiama apre la transazione
    (una per gesto del mouse), cosi' trascinare una nota si annulla in un passo.
*/
namespace sonora::notes
{

/** Arrotonda per difetto alla griglia. */
tracktion::BeatPosition snapDown (tracktion::BeatPosition, tracktion::BeatDuration grid);

/** Arrotonda al punto di griglia piu' vicino. */
tracktion::BeatPosition snapNearest (tracktion::BeatPosition, tracktion::BeatDuration grid);

/** Lunghezza della clip (della sua regione di loop) in beat. */
tracktion::BeatDuration getClipLength (tracktion::MidiClip&);

/** Cambia la lunghezza della clip e del suo loop, in battute. Le note oltre la fine restano ma non suonano. */
void setClipLengthInBars (tracktion::MidiClip&, int bars);

/** Aggiunge una nota limitata alla lunghezza della clip. */
tracktion::MidiNote* addNote (tracktion::MidiClip&, int pitch, tracktion::BeatPosition start,
                              tracktion::BeatDuration length, int velocity = 100);

/** La nota che copre (pitch, beat), o nullptr. */
tracktion::MidiNote* findNote (tracktion::MidiClip&, int pitch, tracktion::BeatPosition);

/** Sposta una nota (inizio e altezza), restando dentro la clip e nel range MIDI. */
void moveNote (tracktion::MidiClip&, tracktion::MidiNote&, tracktion::BeatPosition newStart, int newPitch);

/** Cambia la lunghezza di una nota (minimo minLength). */
void resizeNote (tracktion::MidiClip&, tracktion::MidiNote&, tracktion::BeatDuration newLength,
                 tracktion::BeatDuration minLength);

void setVelocity (tracktion::MidiClip&, tracktion::MidiNote&, int velocity);
void deleteNote (tracktion::MidiClip&, tracktion::MidiNote&);

/** Quantizza l'inizio delle note alla griglia, lasciando invariata la lunghezza. */
void quantise (tracktion::MidiClip&, tracktion::BeatDuration grid);

/** Nome della nota, es. 60 -> "C3" (convenzione con il Do centrale = C3). */
juce::String noteName (int pitch);

} // namespace sonora::notes
