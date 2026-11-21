/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace sonora
{

/** Finestra che mostra l'interfaccia di un plugin.

    Usa la barra del titolo nativa: cosi' il contenuto parte dall'angolo
    della finestra del sistema, che e' dove i plugin CLAP, VST3 e LV2
    posizionano la propria finestra figlia.
*/
class PluginWindow final : public juce::DocumentWindow
{
public:
    explicit PluginWindow (tracktion::Plugin&);
    ~PluginWindow() override;

    static std::unique_ptr<juce::Component> create (tracktion::Plugin&);

    void recreateEditor();
    void recreateEditorAsync();

private:
    void moved() override;
    void closeButtonPressed() override;
    float getDesktopScaleFactor() const override    { return 1.0f; }

    void setEditor (std::unique_ptr<tracktion::Plugin::EditorComponent>);

    tracktion::Plugin& plugin;
    std::unique_ptr<tracktion::Plugin::EditorComponent> editor;
    bool updateStoredBounds = false;
};

/** Contenuto della finestra per i plugin esterni: l'editor del plugin, o un
    editor generico se il plugin non ne ha uno.
*/
class ExternalPluginEditor final : public tracktion::Plugin::EditorComponent
{
public:
    explicit ExternalPluginEditor (tracktion::ExternalPlugin&);

    bool allowWindowResizing() override         { return false; }
    juce::ComponentBoundsConstrainer* getBoundsConstrainer() override;
    void resized() override;
    void childBoundsChanged (juce::Component*) override;

private:
    tracktion::ExternalPlugin& plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
};

} // namespace sonora
