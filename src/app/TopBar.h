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

/** Barra superiore: menu, viste, undo/redo, trasporto, tempo, metro,
    quantizzazione del lancio, palette comandi e CPU.
*/
class TopBar final : public juce::Component,
                     private juce::Timer
{
public:
    explicit TopBar (tracktion::Engine&);
    ~TopBar() override;

    void setEdit (tracktion::Edit*);

    enum class View { session, arrangement };
    void setView (View);

    std::function<void (View)> onViewChanged;
    std::function<void()> onShowMenu, onShowCommandPalette;

    /** Trasporto: li gestisce chi possiede il registratore, che deve chiudere le registrazioni. */
    std::function<void()> onTogglePlay, onStop, onRecord, onCapture;
    std::function<bool()> isRecording, canCapture;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showTimeSigMenu();
    void showQuantisationMenu();

    tracktion::Engine& engine;
    tracktion::Edit* edit = nullptr;

    IconButton menuButton { "Menu", icons::menu() };
    FlatButton sessionButton { "Session" }, arrangementButton { "Arrangement" };
    IconButton undoButton { "Annulla (Ctrl+Z)", icons::undo() }, redoButton { "Ripeti (Ctrl+Shift+Z)", icons::redo() };
    IconButton playButton { "Riproduci (Spazio)", icons::play() }, stopButton { "Stop", icons::stop() },
               recordButton { "Registra nell'arrangement (F9)", icons::record() },
               captureButton { "Cattura quello che hai appena suonato (Ctrl+Shift+C)", icons::capture() },
               loopButton { "Loop", icons::loop() }, metronomeButton { "Metronomo", icons::metronome() };
    DragNumber tempo { 20.0, 999.0, 0.01, 2, "BPM" };
    FlatButton timeSigButton { "4/4", 13.0f }, quantisationButton { "1 battuta", 12.0f };
    FlatButton paletteButton { "Cerca azioni, device, plugin", 12.5f };
    juce::String positionText, cpuText;
};

} // namespace sonora
