/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "host/SandboxedPlugin.h"

/** Plugin isolati dal lato dell'interfaccia: stato di un device, riavvio, scelta. */
namespace sonora::isolation
{
    namespace te = tracktion;

    /** Fa passare la creazione dei plugin di TE di qui: quelli da isolare (secondo le
        impostazioni) si caricano in un processo separato. Su Linux e Windows nessun
        formato chiede la creazione asincrona, quindi TE usa sempre questa funzione. */
    inline void install (te::PluginManager& pm, juce::File hostExecutable = sandbox::findHostExecutable())
    {
        pm.createPluginInstance = [original = pm.createPluginInstance, hostExecutable]
            (const juce::PluginDescription& d, double rate, int block, juce::String& error) -> std::unique_ptr<juce::AudioPluginInstance>
        {
            if (sandbox::shouldIsolate (d) && hostExecutable.existsAsFile())
                return sandbox::createIsolatedInstance (d, rate, block, error, hostExecutable);

            return original (d, rate, block, error);
        };
    }

    inline juce::AudioPluginInstance* instanceOf (const te::Plugin& p)
    {
        if (auto external = dynamic_cast<const te::ExternalPlugin*> (&p))
            return external->getAudioPluginInstance();

        return nullptr;
    }

    inline bool isIsolated (const te::Plugin& p)    { return sandbox::isIsolated (instanceOf (p)); }
    inline bool hasCrashed (const te::Plugin& p)    { return sandbox::hasCrashed (instanceOf (p)); }

    /** Cambia quando cambia l'istanza (riavvio) o quando va in crash: per ridisegnare. */
    inline juce::String tag (const te::Plugin& p)
    {
        auto* instance = instanceOf (p);

        if (instance == nullptr)
            return {};

        return juce::String::toHexString ((juce::pointer_sized_int) instance) + (sandbox::hasCrashed (instance) ? "x" : "");
    }

    /** Ricarica il plugin con lo stato attuale (l'ultimo buono, se il processo e' morto).
        Si decide di nuovo se isolarlo. Fuori dall'annulla: non cambia il progetto. */
    inline void reload (te::ExternalPlugin& p)
    {
        // Spegnere e riaccendere l'elaborazione libera l'istanza e ne crea una nuova.
        // Il valore va cambiato dal CachedValue (che si aggiorna prima di avvisare il
        // plugin) e senza annulla: setProcessingEnabled lo metterebbe nella storia.
        struct Access : te::Plugin
        {
            static juce::CachedValue<bool>& of (te::Plugin& plugin)    { return plugin.*(&Access::processing); }
        };

        p.flushPluginStateToValueTree();
        Access::of (p).setValue (false, nullptr);
        Access::of (p).setValue (true, nullptr);
    }

    /** Ricarica i plugin del progetto che ora vanno (o non vanno piu') isolati, e quelli morti. */
    inline int applyToEdit (te::Edit& edit)
    {
        int reloaded = 0;

        for (auto plugin : te::getAllPlugins (edit, false))
        {
            if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin))
            {
                auto* instance = external->getAudioPluginInstance();

                if (instance == nullptr)
                    continue;

                if (sandbox::hasCrashed (instance) || sandbox::isIsolated (instance) != sandbox::shouldIsolate (external->desc))
                {
                    reload (*external);
                    ++reloaded;
                }
            }
        }

        return reloaded;
    }
}
