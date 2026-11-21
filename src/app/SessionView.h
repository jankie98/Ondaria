/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace sonora::recording { class Recorder; }

namespace sonora
{

/** Session view: una colonna per traccia con gli slot delle clip e il mixer,
    una colonna per le scene e il master a destra.

    - Triangolo di una clip: la lancia. Clic sul resto: la seleziona.
    - Doppio clic su uno slot vuoto: crea una clip MIDI.
    - Trascina un file audio o MIDI su uno slot per creare una clip.
    - Invio lancia la clip selezionata, Canc la elimina, frecce per muoversi.
*/
class SessionView final : public juce::Component,
                          private juce::Timer
{
public:
    SessionView();
    ~SessionView() override;

    void setEdit (tracktion::Edit*);

    /** Registrazione negli slot e armamento delle tracce (puo' essere nullptr). */
    void setRecorder (recording::Recorder*);

    tracktion::AudioTrack* getSelectedTrack() const;
    void selectTrack (tracktion::AudioTrack*);

    /** Seleziona lo slot (e la sua clip) della traccia alla scena data. */
    void selectSlot (tracktion::AudioTrack*, int sceneIndex);

    /** Chiamata quando cambia la traccia selezionata. */
    std::function<void (tracktion::AudioTrack*)> onTrackSelected;

    /** Chiamata quando si seleziona uno slot (clip nullptr se e' vuoto). */
    std::function<void (tracktion::Clip*)> onClipSelected;

    /** Messaggi brevi per la barra di stato. */
    std::function<void (const juce::String&)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class SlotComponent;
    class TrackColumn;
    class SceneColumn;
    class Content;

    void timerCallback() override;
    void rebuildIfNeeded (bool force = false);
    tracktion::ClipSlot* getSelectedSlot() const;
    void status (const juce::String&);

    tracktion::Edit* edit = nullptr;
    recording::Recorder* recorder = nullptr;
    tracktion::EditItemID selectedTrackID;
    int selectedScene = 0;
    juce::String gridSignature;

    juce::Viewport viewport;
    std::unique_ptr<Content> content;
};

} // namespace sonora
