/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "PluginFormats.h"
#include "ClapPluginFormat.h"

namespace sonora
{

static bool hasFormat (juce::AudioPluginFormatManager& manager, const juce::String& name)
{
    for (auto* f : manager.getFormats())
        if (f->getName() == name)
            return true;

    return false;
}

void addAllPluginFormats (juce::AudioPluginFormatManager& manager)
{
    if (manager.getNumFormats() == 0)
        juce::addDefaultFormatsToManager (manager);

    if (! hasFormat (manager, ClapPluginFormat::formatName))
        manager.addFormat (std::make_unique<ClapPluginFormat>());
}

juce::StringArray getSupportedFormatNames()
{
    juce::AudioPluginFormatManager manager;
    addAllPluginFormats (manager);

    juce::StringArray names;

    for (auto* f : manager.getFormats())
        names.add (f->getName());

    return names;
}

} // namespace sonora
