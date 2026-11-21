/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

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

} // namespace sonora
