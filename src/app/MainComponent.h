/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "ArrangementView.h"
#include "CommandPalette.h"
#include "ExportPanel.h"
#include "DeviceChainComponent.h"
#include "PianoRoll.h"
#include "PluginBrowserComponent.h"
#include "SessionView.h"
#include "TopBar.h"
#include "session/Automation.h"
#include "session/Capture.h"
#include "session/Recording.h"

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
    void showAudioSettings();
    void showMainMenu();
    void toggleTheme();
    void togglePerformance();
    int browserWidth() const;
    juce::PopupMenu scaleMenu();
    void setUiScale (float);
    void showCommandPalette();
    void setView (TopBar::View);

    enum class Detail { devices, clip, keyboard };
    void setDetail (Detail);
    std::function<void (tracktion::Clip*)> showClip;
    int getDetailHeight() const;
    int targetDetailHeight() const;
    void animateDetail();
    void togglePlay();
    void stopTransport (bool returnToStartIfStopped);
    void toggleRecord();
    void captureNow();
    void showExport();
    void status (const juce::String&);

    tracktion::Engine& engine;
    std::unique_ptr<capture::Capturer> capturer;      // ascolta sempre, anche cambiando progetto
    std::unique_ptr<tracktion::Edit> edit;
    std::unique_ptr<recording::Recorder> recorder;   // dopo edit: va distrutto prima
    std::shared_ptr<tracktion::MidiInputDevice> keyboardDevice;
    std::unique_ptr<juce::FileChooser> chooser;

    TopBar topBar;
    PluginBrowserComponent browser;
    SessionView sessionView;
    ArrangementView arrangement;
    DeviceChainComponent deviceChain;
    PianoRoll pianoRoll;
    Detail detail = Detail::devices;
    FlatButton devicesTab { "Device", 12.0f }, clipTab { "Clip", 12.0f };
    CommandPalette palette;
    ExportPanel exportPanel;

    IconButton keyboardButton { "Tastiera MIDI su schermo", icons::keyboard() };
    FlatButton addDeviceButton { "+ Aggiungi device", 12.0f };
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    juce::String statusText, hintText;
    juce::TooltipWindow tooltipWindow { this, 700 };
    juce::uint32 statusTime = 0;
    TopBar::View view = TopBar::View::session;

    // Altezza del pannello dettaglio mentre scorre verso quella nuova
    float detailHeightNow = 0.0f;
    bool wasFullScreenBeforePerformance = false;
    bool detailAnimating = false;
    juce::VBlankAttachment vblank { this, [this] { animateDetail(); } };
};

} // namespace sonora
