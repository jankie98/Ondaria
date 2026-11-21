/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace sonora
{

/** Formato plugin CLAP per l'AudioPluginFormatManager di JUCE.

    JUCE 9 non include un host CLAP, quindi questo adattatore implementa il
    lato host dello standard CLAP (https://github.com/free-audio/clap) e lo
    espone come un normale formato JUCE: Tracktion Engine, la scansione e il
    browser trattano i plugin CLAP come VST3 o LV2.

    Un file .clap puo' contenere piu' plugin. Ogni plugin e' descritto con
    fileOrIdentifier = percorso del file e uniqueId = hash dell'id CLAP.
*/
class ClapPluginFormat final : public juce::AudioPluginFormat
{
public:
    ClapPluginFormat() = default;

    static constexpr const char* formatName = "CLAP";

    juce::String getName() const override                       { return formatName; }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>&,
                              const juce::String& fileOrIdentifier) override;

    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override;
    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override;
    bool pluginNeedsRescanning (const juce::PluginDescription&) override;
    bool doesPluginStillExist (const juce::PluginDescription&) override;
    bool canScanForPlugins() const override                     { return true; }
    bool isTrivialToScan() const override                       { return false; }

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool recursive,
                                             bool allowAsync = false) override;
    juce::FileSearchPath getDefaultLocationsToSearch() override;

    /** Cartelle standard dei plugin CLAP per la piattaforma, piu' CLAP_PATH. */
    static juce::FileSearchPath getDefaultSearchPath();

    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override  { return false; }

    /** L'hash usato come uniqueId per un id CLAP (es. "org.surge-synth-team.surge-xt"). */
    static int uniqueIdForClapId (const juce::String& clapId);

private:
    void createPluginInstance (const juce::PluginDescription&, double initialSampleRate,
                               int initialBufferSize, PluginCreationCallback) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClapPluginFormat)
};

} // namespace sonora
