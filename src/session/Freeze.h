/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Freeze: una traccia pesante (synth e effetti) viene calcolata una volta in un file
    audio e poi suona da quello, senza usare la CPU dei suoi device. Si scongela per
    modificarla di nuovo, o si "appiattisce" per trasformarla per sempre in audio.

    Il freeze riguarda la linea del tempo: congelando, la traccia torna a suonare
    l'arrangement. I device dopo il fader (e il fader stesso) restano vivi.
*/
namespace sonora::freeze
{

bool isFrozen (const tracktion::AudioTrack&);

/** Si puo' congelare? Se no, why riceve il motivo. */
bool canFreeze (tracktion::AudioTrack&, juce::String* why = nullptr);

/** Calcola il file e congela. Blocca finche' il render non e' finito (l'interfaccia
    resta viva tramite UIBehaviour::runTaskWithProgressBar). */
juce::Result freeze (tracktion::AudioTrack&);

void unfreeze (tracktion::AudioTrack&);

/** Sostituisce clip e device prima del fader con una clip audio del risultato
    (congela prima, se serve). Si annulla con Ctrl+Z. La clip nuova va in flatClip. */
juce::Result flatten (tracktion::AudioTrack&, tracktion::Clip** flatClip = nullptr);

} // namespace sonora::freeze
