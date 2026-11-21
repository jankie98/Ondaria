/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sonora
{

/** Registra tutti i formati plugin supportati: quelli di JUCE (VST3, LV2,
    LADSPA su Linux, VST2 se compilato con l'SDK) piu' il nostro CLAP.
    Se il manager contiene gia' i formati di JUCE, aggiunge solo quelli mancanti.
*/
void addAllPluginFormats (juce::AudioPluginFormatManager&);

/** Nomi dei formati compilati in questa build, per l'interfaccia. */
juce::StringArray getSupportedFormatNames();

} // namespace sonora
