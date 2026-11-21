/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Automazioni: curve dei parametri nel tempo (volume, pan e parametri dei device)
    e corsie dell'arrangement dove si vedono e si disegnano.
*/
namespace sonora::automation
{

/** Un parametro automatizzabile di una traccia. */
struct ParameterInfo
{
    tracktion::AutomatableParameter::Ptr param;
    juce::String key;    ///< identifica il parametro nella traccia (resta valido salvando)
    juce::String name;   ///< "Mixer: Volume", "EQ: Low-shelf gain"...
};

/** Volume e pan della traccia, poi i parametri dei device nella catena. */
std::vector<ParameterInfo> getParameters (tracktion::AudioTrack&);

juce::String keyFor (tracktion::AutomatableParameter&);
tracktion::AutomatableParameter::Ptr findParameter (tracktion::AudioTrack&, const juce::String& key);
juce::String displayName (tracktion::AutomatableParameter&);

/** Corsie di automazione mostrate sotto la traccia (chiavi, nell'ordine in cui sono state aperte). */
juce::StringArray getShownLanes (const tracktion::AudioTrack&);
bool isLaneShown (const tracktion::AudioTrack&, const juce::String& key);
void setLaneShown (tracktion::AudioTrack&, const juce::String& key, bool shown);

/** Il parametro ha dei punti? */
bool hasAutomation (tracktion::AutomatableParameter&);

/** Almeno un parametro della traccia ha dei punti? */
bool hasAutomation (tracktion::AudioTrack&);

/** Mostra le corsie di tutti i parametri con dei punti. Restituisce quante ne ha aperte. */
int showAutomatedLanes (tracktion::AudioTrack&);

/** Nasconde tutte le corsie della traccia. */
void hideAllLanes (tracktion::AudioTrack&);

/** Valore del parametro tra 0 e 1 (come lo si disegna nella corsia). */
float toNormalised (const tracktion::AutomatableParameter&, float value);
float fromNormalised (const tracktion::AutomatableParameter&, float normalised);

/** Testo leggibile per un valore del parametro ("-6.0 dB", "40%"...). */
juce::String valueText (tracktion::AutomatableParameter&, float value);

/** Aggiunge un punto (valore 0..1) e ne restituisce l'indice. */
int addPoint (tracktion::AutomatableParameter&, tracktion::TimePosition, float normalised);

/** Sposta un punto; restituisce il nuovo indice (l'ordine nel tempo puo' cambiare). */
int movePoint (tracktion::AutomatableParameter&, int index, tracktion::TimePosition, float normalised);

void removePoint (tracktion::AutomatableParameter&, int index);
void clear (tracktion::AutomatableParameter&);

/** Curvatura del tratto che parte dal punto (-1..1, 0 = retta). */
void setCurve (tracktion::AutomatableParameter&, int index, float curve);

/** Valore della curva (0..1) in un istante, o il valore attuale se non ci sono punti. */
float normalisedValueAt (tracktion::AutomatableParameter&, tracktion::TimePosition);

/** Scrittura delle automazioni: i movimenti delle manopole mentre il brano suona diventano punti. */
bool isWriting (tracktion::Edit&);
void setWriting (tracktion::Edit&, bool);

/** Da chiamare quando si apre un progetto: la scrittura parte sempre spenta. */
void prepareEdit (tracktion::Edit&);

/** Mostra le corsie dei parametri che si stanno scrivendo adesso. True se ne ha aperta qualcuna. */
bool showLanesBeingWritten (tracktion::Edit&);

} // namespace sonora::automation
