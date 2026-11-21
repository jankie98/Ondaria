/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "DeviceChainComponent.h"
#include "PluginBrowserComponent.h"

namespace sonora
{

/** Schermata principale della prima versione: trasporto, browser dei
    device, elenco tracce, catena di device e tastiera MIDI di prova.
    Session view e arrangement arrivano nel passo 3.
*/
class MainComponent final : public juce::Component,
                            private juce::ListBoxModel,
                            private juce::ChangeListener,
                            private juce::MidiKeyboardState::Listener,
                            private juce::Timer
{
public:
    explicit MainComponent (tracktion::Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Salva il progetto corrente. */
    void save();

private:
    // Elenco tracce
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void timerCallback() override;

    void createOrLoadEdit();
    void insertDevice (const juce::PluginDescription&);
    tracktion::AudioTrack* getSelectedTrack() const;
    void routeKeyboardTo (tracktion::AudioTrack*);
    void showAudioSettings();
    void updateTransportButtons();

    tracktion::Engine& engine;
    std::unique_ptr<tracktion::Edit> edit;
    std::shared_ptr<tracktion::MidiInputDevice> keyboardDevice;

    juce::TextButton audioButton { "Audio..." }, playButton { "Play" }, stopButton { "Stop" },
                     addTrackButton { "+ Traccia" };
    juce::Slider tempoSlider { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label tempoLabel { {}, "BPM" }, cpuLabel, statusLabel;

    PluginBrowserComponent browser;
    juce::ListBox trackList;
    DeviceChainComponent deviceChain;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
};

} // namespace sonora
