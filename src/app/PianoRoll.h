/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "Widgets.h"

namespace sonora
{

/** Editor delle note di una clip MIDI, nel pannello dettaglio.

    - Clic su uno spazio vuoto: nuova nota (trascina per allungarla).
    - Trascina una nota per spostarla, dal bordo destro per allungarla.
    - Doppio clic o clic destro su una nota: la elimina.
    - Barre in basso: velocity delle note.
    - Canc elimina le selezionate, frecce su/giu' trasportano, Ctrl+A seleziona tutto.
    - Rotella: scorre le altezze. Ctrl+rotella: zoom orizzontale.
*/
class PianoRoll final : public juce::Component,
                        private juce::Timer
{
public:
    PianoRoll();
    ~PianoRoll() override;

    void setClip (tracktion::MidiClip*);
    tracktion::MidiClip* getClip() const        { return clip.get(); }

    /** Suona un'anteprima della nota (true = note on). */
    std::function<void (int pitch, bool on)> onPreviewNote;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Grid;

    void timerCallback() override;
    void updateToolbar();
    void showGridMenu();
    void showLengthMenu();
    bool isFrozen() const;

    tracktion::MidiClip::Ptr clip;
    FlatButton gridButton { "1/16", 12.0f }, lengthButton { "1 battuta", 12.0f },
               quantiseButton { "Quantizza", 12.0f }, foldButton { "Zoom note", 12.0f };
    std::unique_ptr<Grid> grid;
};

} // namespace sonora
