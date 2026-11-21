/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "CommandPalette.h"
#include "DeviceChainComponent.h"
#include "PluginBrowserComponent.h"
#include "SessionView.h"
#include "TopBar.h"

namespace sonora
{

/** Finestra principale: barra superiore, browser a sinistra, session view al
    centro, pannello dettaglio (catena di device o tastiera) in basso.
*/
class MainComponent final : public juce::Component,
                            public juce::DragAndDropContainer,
                            private juce::MidiKeyboardState::Listener,
                            private juce::Timer
{
public:
    explicit MainComponent (tracktion::Engine&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Salva il progetto corrente nel suo file. */
    void save();

    /** Usato all'uscita: salva solo se ci sono modifiche. */
    void saveIfChanged();

private:
    class ArrangementPlaceholder;

    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void timerCallback() override;

    void setEdit (std::unique_ptr<tracktion::Edit>);
    void newProject();
    void openProject();
    void openProject (const juce::File&);
    void saveAs();
    static juce::File getProjectsFolder();

    void insertDevice (const juce::PluginDescription&);
    void routeKeyboardTo (tracktion::AudioTrack*);
    void showAudioSettings();
    void showMainMenu();
    void showCommandPalette();
    void setView (TopBar::View);
    void togglePlay();
    void status (const juce::String&);

    tracktion::Engine& engine;
    std::unique_ptr<tracktion::Edit> edit;
    std::shared_ptr<tracktion::MidiInputDevice> keyboardDevice;
    std::unique_ptr<juce::FileChooser> chooser;

    TopBar topBar;
    PluginBrowserComponent browser;
    SessionView sessionView;
    std::unique_ptr<ArrangementPlaceholder> arrangement;
    DeviceChainComponent deviceChain;
    CommandPalette palette;

    IconButton keyboardButton { "Tastiera MIDI su schermo", icons::keyboard() };
    FlatButton addDeviceButton { "+ Aggiungi device", 12.0f };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    juce::String statusText;
    juce::uint32 statusTime = 0;
    TopBar::View view = TopBar::View::session;
};

} // namespace sonora
