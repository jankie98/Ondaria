/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "PluginWindow.h"

namespace te = tracktion;

namespace sonora
{

//==============================================================================
ExternalPluginEditor::ExternalPluginEditor (te::ExternalPlugin& p) : plugin (p)
{
    if (auto instance = plugin.getAudioPluginInstance())
    {
        editor.reset (instance->createEditorAndMakeActive());

        if (editor == nullptr)
            editor = std::make_unique<juce::GenericAudioProcessorEditor> (*instance);

        addAndMakeVisible (*editor);
        setSize (juce::jmax (8, editor->getWidth()), juce::jmax (8, editor->getHeight()));
    }
    else
    {
        setSize (300, 100);
    }
}

juce::ComponentBoundsConstrainer* ExternalPluginEditor::getBoundsConstrainer()
{
    return editor != nullptr ? editor->getConstrainer() : nullptr;
}

void ExternalPluginEditor::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds());
}

void ExternalPluginEditor::childBoundsChanged (juce::Component* c)
{
    if (c == editor.get())
    {
        plugin.edit.pluginChanged (plugin);
        setSize (juce::jmax (8, editor->getWidth()), juce::jmax (8, editor->getHeight()));
    }
}

//==============================================================================
PluginWindow::PluginWindow (te::Plugin& p)
    : DocumentWindow (p.getName(), juce::Colours::black, DocumentWindow::closeButton, false),
      plugin (p)
{
    setUsingNativeTitleBar (true);
    getConstrainer()->setMinimumOnscreenAmounts (0x10000, 50, 30, 50);
    setResizeLimits (100, 50, 4000, 4000);

    recreateEditor();

    setBoundsConstrained (getLocalBounds() + plugin.windowState->choosePositionForPluginWindow());
    setAlwaysOnTop (true);
    addToDesktop();
    updateStoredBounds = true;
}

PluginWindow::~PluginWindow()
{
    updateStoredBounds = false;
    plugin.edit.flushPluginStateIfNeeded (plugin);
    setEditor (nullptr);
}

std::unique_ptr<juce::Component> PluginWindow::create (te::Plugin& plugin)
{
    if (auto external = dynamic_cast<te::ExternalPlugin*> (&plugin))
        if (external->getAudioPluginInstance() == nullptr)
            return {};

    auto w = std::make_unique<PluginWindow> (plugin);
    w->setVisible (true);
    w->toFront (false);
    return w;
}

void PluginWindow::setEditor (std::unique_ptr<te::Plugin::EditorComponent> newEditor)
{
    setConstrainer (nullptr);
    clearContentComponent();
    editor = std::move (newEditor);

    if (editor != nullptr)
    {
        setContentNonOwned (editor.get(), true);
        setResizable (editor->allowWindowResizing(), false);

        if (editor->allowWindowResizing())
            setConstrainer (editor->getBoundsConstrainer());
    }
}

void PluginWindow::recreateEditor()
{
    setEditor (nullptr);

    if (auto external = dynamic_cast<te::ExternalPlugin*> (&plugin))
        setEditor (std::make_unique<ExternalPluginEditor> (*external));
    else
        setEditor (plugin.createEditor());
}

void PluginWindow::recreateEditorAsync()
{
    setEditor (nullptr);

    juce::Timer::callAfterDelay (50, [sp = juce::Component::SafePointer<PluginWindow> (this)]
    {
        if (sp != nullptr)
            sp->recreateEditor();
    });
}

void PluginWindow::moved()
{
    if (updateStoredBounds)
    {
        plugin.windowState->lastWindowBounds = getBounds();
        plugin.edit.pluginChanged (plugin);
    }
}

void PluginWindow::closeButtonPressed()
{
    plugin.windowState->closeWindowExplicitly();
}

} // namespace sonora
