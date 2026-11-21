/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace sonora
{

/** Browser dei device: strumenti ed effetti integrati e tutti i plugin
    trovati dalla scansione, di qualsiasi formato, con ricerca per nome.
    Doppio clic (o Invio) inserisce il device nella traccia selezionata.
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

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    juce::String getTooltipForRow (int row) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildList();
    void openPluginManager();

    tracktion::Engine& engine;
    juce::Label title { {}, "Browser" };
    juce::TextEditor search;
    juce::TextButton manageButton { "Gestisci e scansiona plugin..." };
    juce::ListBox list;

    juce::Array<juce::PluginDescription> allItems, visibleItems;
    std::unique_ptr<juce::DialogWindow> managerWindow;
};

} // namespace sonora
