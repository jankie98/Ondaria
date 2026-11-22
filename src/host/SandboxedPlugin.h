/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/** Plugin isolati: ognuno gira in un processo suo (ondaria-plugin-scanner --host).

    Se il plugin va in crash o si blocca, muore solo il suo processo: la DAW manda
    silenzio al suo posto, avvisa e continua. Il prezzo e' un passaggio tra processi
    per ogni blocco audio (memoria condivisa, niente copie di troppo) e la finestra
    del plugin, che si apre a parte.

    La descrizione del plugin resta quella vera, quindi nel progetto salvato non
    cambia niente: alla riapertura si decide di nuovo se isolarlo.
*/
namespace sonora::sandbox
{
    enum class Policy
    {
        none,       ///< tutti i plugin dentro la DAW
        chosen,     ///< solo quelli scelti dall'utente
        all         ///< ogni plugin nel suo processo
    };

    /** Impostazioni correnti (le carica l'app all'avvio). */
    Policy getPolicy();
    void setPolicy (Policy);
    juce::String policyName (Policy);

    /** Identificatori (PluginDescription::createIdentifierString) dei plugin scelti. */
    juce::StringArray getChosenPlugins();
    void setChosenPlugins (const juce::StringArray&);
    bool isChosen (const juce::PluginDescription&);
    void setChosen (const juce::PluginDescription&, bool);

    /** Va isolato secondo le impostazioni? */
    bool shouldIsolate (const juce::PluginDescription&);

    /** L'eseguibile che ospita i plugin (accanto a quello corrente). */
    juce::File findHostExecutable();

    /** Carica il plugin in un processo separato. nullptr con errorMessage se non ci riesce
        (anche se il plugin va in crash mentre si carica). */
    std::unique_ptr<juce::AudioPluginInstance> createIsolatedInstance (const juce::PluginDescription&,
                                                                       double sampleRate, int blockSize,
                                                                       juce::String& errorMessage,
                                                                       const juce::File& hostExecutable = findHostExecutable());

    /** L'istanza gira in un processo separato. */
    bool isIsolated (const juce::AudioProcessor*);

    /** Il processo del plugin e' morto (crash o blocco): l'istanza ora tace. */
    bool hasCrashed (const juce::AudioProcessor*);

    /** Chiamata sul thread dei messaggi quando un plugin isolato muore. */
    void setCrashCallback (std::function<void (const juce::String& pluginName)>);

    /** Avvia il processo ospite (per ondaria-plugin-scanner): se la riga di comando
        e' quella di un plugin isolato lo serve e restituisce true alla fine. */
    bool runHostIfRequested (const juce::String& commandLine);
}
