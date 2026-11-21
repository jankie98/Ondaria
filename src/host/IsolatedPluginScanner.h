/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sonora
{

/** Scansione dei plugin in un processo separato.

    Ogni file viene aperto da sonora-plugin-scanner. Se il plugin va in crash,
    si blocca oltre il timeout o lo scanner non parte, restituiamo false e
    KnownPluginList mette il file nella lista di esclusione: la DAW resta viva.
*/
class IsolatedPluginScanner final : public juce::KnownPluginList::CustomScanner
{
public:
    explicit IsolatedPluginScanner (juce::File scannerExecutable = findScannerExecutable(),
                                    int timeoutMs = 60000);

    bool findPluginTypesFor (juce::AudioPluginFormat&,
                             juce::OwnedArray<juce::PluginDescription>& result,
                             const juce::String& fileOrIdentifier) override;

    /** Cerca lo scanner accanto all'eseguibile corrente. */
    static juce::File findScannerExecutable();

    enum class Outcome { found, crashed, timedOut, cancelled, scannerMissing };

    /** Scansiona un file in un processo separato; usabile anche fuori da KnownPluginList. */
    Outcome scanFile (const juce::String& formatName, const juce::String& fileOrIdentifier,
                      juce::OwnedArray<juce::PluginDescription>& result,
                      std::function<bool()> shouldCancel = {}) const;

private:
    const juce::File scanner;
    const int timeoutMs;
};

} // namespace sonora
