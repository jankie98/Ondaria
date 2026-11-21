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

/** Browser a sinistra con una sola ricerca e filtri a etichette.

    - Device: strumenti ed effetti integrati e tutti i plugin trovati dalla
      scansione (CLAP, VST3, LV2, ...). Doppio clic o Invio inseriscono il
      device nella traccia selezionata.
    - Campioni: file audio e MIDI di una cartella, da trascinare sugli slot.
*/
class PluginBrowserComponent final : public juce::Component,
                                     private juce::ListBoxModel,
                                     private juce::ChangeListener
{
public:
    explicit PluginBrowserComponent (tracktion::Engine&);
    ~PluginBrowserComponent() override;

    /** Formato usato per i device integrati di Tracktion. */
    static constexpr const char* builtInFormat = "Integrato";

    std::function<void (const juce::PluginDescription&)> onPluginChosen;

    /** Tutti i device disponibili (integrati e plugin), per la palette comandi. */
    juce::Array<juce::PluginDescription> getAllDevices() const     { return allItems; }

    void focusSearch()      { search.grabKeyboardFocus(); }
    void openPluginManager();

    void resized() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    enum class Filter { all, instruments, effects, plugins, samples };

    struct Row
    {
        juce::String name, detail;
        juce::Colour dot;
        juce::PluginDescription device;
        juce::File file;
    };

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::String getTooltipForRow (int row) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildDevices();
    void rebuildRows();
    void setFilter (Filter);
    void chooseSampleFolder();

    tracktion::Engine& engine;
    juce::TextEditor search;
    juce::OwnedArray<FlatButton> chips;
    FlatButton manageButton { "Gestisci plugin...", 12.0f }, folderButton { "Cartella...", 12.0f };
    juce::ListBox list;

    Filter filter = Filter::all;
    juce::File sampleFolder;
    juce::Array<juce::PluginDescription> allItems;
    std::vector<Row> rows;
    std::unique_ptr<juce::DialogWindow> managerWindow;
    std::unique_ptr<juce::FileChooser> chooser;
};

} // namespace sonora
