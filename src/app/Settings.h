/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "host/SandboxedPlugin.h"

namespace sonora
{

/** Impostazioni dell'interfaccia di Ondaria (tema, cartella dei campioni...), accanto a quelle del motore. */
inline std::unique_ptr<juce::PropertiesFile> openUiSettings (tracktion::Engine& e)
{
    juce::PropertiesFile::Options o;
    return std::make_unique<juce::PropertiesFile> (e.getPropertyStorage().getAppPrefsFolder()
                                                     .getChildFile ("sonora-interfaccia.xml"), o);
}

/** Scala dell'interfaccia (1.0 = 100%), per gli schermi ad alta densita'. Si somma
    a quella che il sistema comunica a JUCE. */
inline float loadUiScale (juce::PropertySet& p)
{
    return juce::jlimit (0.75f, 3.0f, (float) p.getDoubleValue ("scala", 1.0));
}

inline void applyUiScale (float scale)
{
    juce::Desktop::getInstance().setGlobalScaleFactor (scale);
}

/** Plugin isolati: quali far girare in un processo separato. */
inline void loadSandboxSettings (juce::PropertySet& p)
{
    const int policy = p.getIntValue ("isolamentoPlugin", (int) sandbox::Policy::chosen);
    sandbox::setPolicy ((sandbox::Policy) juce::jlimit (0, 2, policy));
    auto chosen = juce::StringArray::fromLines (p.getValue ("pluginIsolati"));
    chosen.removeEmptyStrings();
    sandbox::setChosenPlugins (chosen);
}

inline void saveSandboxSettings (tracktion::Engine& e)
{
    if (auto settings = openUiSettings (e))
    {
        settings->setValue ("isolamentoPlugin", (int) sandbox::getPolicy());
        settings->setValue ("pluginIsolati", sandbox::getChosenPlugins().joinIntoString ("\n"));
        settings->saveIfNeeded();
    }
}

} // namespace sonora
