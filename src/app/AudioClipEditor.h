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

/** Disegna la forma d'onda di una clip audio nel rettangolo che occupa sulla linea del
    tempo, seguendo warp, marker, velocita' e loop (a tratti, cosi' i tratti stirati si
    vedono stirati). */
void drawWarpedWaveform (juce::Graphics&, tracktion::SmartThumbnail&, const tracktion::AudioClipBase&,
                         juce::Rectangle<float> area, juce::Colour);

/** Pannello della clip audio nel dettaglio: warp, tempo originale, trasposizione, volume,
    e la forma d'onda con i marker di warp.

    - Doppio clic sulla forma d'onda: nuovo marker in quel punto del file.
    - Trascina un marker per stirare l'audio prima e dopo (Ctrl: senza griglia).
    - Doppio clic su un marker: lo toglie. Clic destro: menu.
    - Rotella per scorrere, Ctrl+rotella per lo zoom, doppio clic sul righello: adatta.
*/
class AudioClipEditor final : public juce::Component,
                              public HintProvider,
                              private juce::Timer
{
public:
    AudioClipEditor();
    ~AudioClipEditor() override;

    void setClip (tracktion::WaveAudioClip*);
    tracktion::WaveAudioClip* getClip() const      { return clip.get(); }

    /** Messaggi per la barra di stato. */
    std::function<void (const juce::String&)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    juce::String getHintAt (juce::Point<int>) override;

private:
    class WaveView;

    void timerCallback() override;
    void refreshControls();
    void showModeMenu();
    bool isFrozen() const;
    void status (const juce::String&);

    tracktion::WaveAudioClip::Ptr clip;
    FlatButton warpButton { "Warp", 12.0f }, modeButton { "Completo", 12.0f },
               halfButton { juce::String::fromUTF8 ("\xc3\xb7" "2"), 12.0f },
               doubleButton { juce::String::fromUTF8 ("\xc3\x97" "2"), 12.0f },
               reverseButton { "Al contrario", 12.0f }, resetButton { "Togli i marker", 12.0f };
    DragNumber tempo { 20.0, 999.0, 0.01, 2, "BPM" }, transpose { -48.0, 48.0, 1.0, 0, "st" },
               detune { -50.0, 50.0, 1.0, 0, "ct" }, gain { -70.0, 24.0, 0.1, 1, "dB" };
    std::unique_ptr<WaveView> wave;
    juce::Rectangle<int> labelsArea;
};

} // namespace sonora
