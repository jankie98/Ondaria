/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>
#include "PluginWindow.h"
#include "TaskOverlay.h"

namespace sonora
{

/** Comportamento dell'interfaccia richiesto da Tracktion Engine: qui
    decidiamo come si aprono le finestre dei plugin.
*/
class SonoraUIBehaviour final : public tracktion::UIBehaviour
{
public:
    std::unique_ptr<juce::Component> createPluginWindow (tracktion::PluginWindowState& state) override
    {
        if (auto ws = dynamic_cast<tracktion::Plugin::WindowState*> (&state))
            return PluginWindow::create (ws->plugin);

        return {};
    }

    void recreatePluginWindowContentAsync (tracktion::Plugin& p) override
    {
        if (auto* w = dynamic_cast<PluginWindow*> (p.windowState->pluginWindow.get()))
            return w->recreateEditorAsync();

        UIBehaviour::recreatePluginWindowContentAsync (p);
    }

    // Il render del freeze: gira in sottofondo con un velo e la barra di avanzamento
    void runTaskWithProgressBar (tracktion::ThreadPoolJobWithProgress& job) override
    {
        TaskOverlay::run (job);
    }
};

class SonoraEngineBehaviour final : public tracktion::EngineBehaviour
{
public:
    // La scansione fuori processo la fa il nostro IsolatedPluginScanner.
    bool canScanPluginsOutOfProcess() override  { return false; }
};

} // namespace sonora
