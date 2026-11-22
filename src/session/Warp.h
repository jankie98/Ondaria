/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Warp: le clip audio seguono il tempo del progetto senza cambiare altezza.

    Ogni clip ha un tempo originale (quello a cui e' stata suonata). Con il warp acceso
    la clip e' fissata alle battute: se il progetto va a 100 BPM e il loop era a 120,
    il motore lo allunga. Il modo sceglie l'algoritmo, oppure "Ripitch" che cambia
    velocita' come un giradischi (e quindi anche l'altezza).

    I marker di warp fissano un punto del file a un punto della clip: spostandoli si
    allunga o si accorcia solo l'audio tra un marker e l'altro.
*/
namespace sonora::warp
{

enum class Mode
{
    complete,   ///< Signalsmith, qualita' alta: va bene quasi per tutto
    light,      ///< Signalsmith economico, per quando la CPU serve altrove
    repitch     ///< Niente stretch: cambia la velocita' e l'altezza insieme
};

juce::String name (Mode);
juce::Array<Mode> allModes();

//==============================================================================
/** Il tempo (BPM) piu' probabile per un file lungo seconds.
    Se la durata e' un numero intero di battute a un tempo ragionevole (70-180 BPM)
    si sceglie quello, vicino a detected se c'e' (o a 120); altrimenti detected
    riportato tra 70 e 180; se non c'e' nemmeno quello, fallback. */
double guessTempo (double seconds, double detectedBpm, int beatsPerBar, double fallback);

/** Stima il tempo del file dagli attacchi (energia, autocorrelazione); 0 se non si capisce. */
double detectTempo (tracktion::Engine&, const juce::File&);

/** Lo stesso su un inviluppo di energia gia' calcolato (log dell'energia per fotogramma). */
double detectTempo (const float* envelope, int numFrames, double framesPerSecond);

//==============================================================================
bool isWarped (const tracktion::AudioClipBase&);
Mode getMode (const tracktion::AudioClipBase&);

/** Il tempo a cui e' stato suonato il file della clip. */
double getOriginalTempo (const tracktion::AudioClipBase&);

/** Accende o spegne il warp. Spento, la clip suona com'e' alla velocita' originale. */
void setWarped (tracktion::AudioClipBase&, bool);

void setMode (tracktion::AudioClipBase&, Mode);

/** Cambia il tempo originale: la clip si allunga o si accorcia sulla griglia
    (a 60 invece di 120 occupa il doppio delle battute). */
void setOriginalTempo (tracktion::AudioClipBase&, double bpm);

/** Clip appena importata: stima il tempo e accende il warp, come fa un DAW moderno
    con loop e brani interi. Le clip molto corte (colpi singoli) restano senza warp. */
void prepareImported (tracktion::AudioClipBase&);

/** Le clip in Ripitch cambiano velocita' quando cambia il tempo del progetto:
    va chiamata quando il tempo cambia. Restituisce quante clip ha aggiornato. */
int followTempo (tracktion::Edit&);

/** Il punto del file (in secondi) che suona al tempo dato del progetto, tenendo conto
    di warp, marker, velocita' e loop. Fuori dalla clip: niente. */
std::optional<double> sourceSecondsAt (const tracktion::AudioClipBase&, tracktion::TimePosition);

/** Conversioni tra un punto del file (secondi) e la sua posizione nella clip, in beat dal
    suo inizio al tempo originale, tenendo conto dei marker. */
double beatForSource (const tracktion::AudioClipBase&, double sourceSeconds);
double sourceForBeat (const tracktion::AudioClipBase&, double beat);

/** Durata del file in secondi. */
double sourceLength (const tracktion::AudioClipBase&);

//==============================================================================
/** Trasposizione in semitoni (-48..48) e accordatura fine in cent (-50..50). */
int getTranspose (const tracktion::AudioClipBase&);
int getDetune (const tracktion::AudioClipBase&);
void setTranspose (tracktion::AudioClipBase&, int semitones);
void setDetune (tracktion::AudioClipBase&, int cents);

//==============================================================================
/** Marker di warp: tempo nel file e posizione nella clip in beat dall'inizio del file. */
struct Marker
{
    double sourceSeconds = 0.0;
    double beat = 0.0;
};

/** I marker della clip, ordinati. Senza warp a marker la lista e' vuota. */
std::vector<Marker> getMarkers (const tracktion::AudioClipBase&);

/** Aggiunge un marker nel punto del file dato; lo mette dove ora suona quel punto.
    Restituisce l'indice o -1. */
int addMarker (tracktion::AudioClipBase&, double sourceSeconds);

/** Sposta il marker al beat dato (dall'inizio del file); resta tra i vicini. */
void moveMarker (tracktion::AudioClipBase&, int index, double beat);

/** Toglie un marker (il primo e l'ultimo restano). */
void removeMarker (tracktion::AudioClipBase&, int index);

/** Toglie tutti i marker aggiunti e torna allo stretch uniforme. */
void resetMarkers (tracktion::AudioClipBase&);

} // namespace sonora::warp
