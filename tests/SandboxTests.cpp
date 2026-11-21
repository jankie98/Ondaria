/*
    Sonora - test dei plugin isolati (un processo per plugin).
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/ClapPluginFormat.h"
#include "host/SandboxedPlugin.h"

namespace
{
namespace sandbox = sonora::sandbox;

const juce::File gainPlugin       { SONORA_TEST_GAIN_PLUGIN };
const juce::File loadCrashPlugin  { SONORA_TEST_CRASH_PLUGIN };
const juce::File processCrashPlugin { SONORA_TEST_PROCESS_CRASH_PLUGIN };
const juce::File hostExecutable   { SONORA_SCANNER_PATH };

juce::PluginDescription describe (const juce::File& file)
{
    sonora::ClapPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, file.getFullPathName());
    return found.isEmpty() ? juce::PluginDescription() : *found[0];
}

/** Il plugin che va in crash appena caricato non si puo' descrivere qui (porterebbe giu'
    i test): la descrizione la scriviamo a mano. */
juce::PluginDescription describeWithoutLoading (const juce::File& file)
{
    juce::PluginDescription d;
    d.name = file.getFileNameWithoutExtension();
    d.pluginFormatName = "CLAP";
    d.fileOrIdentifier = file.getFullPathName();
    d.uniqueId = 1;
    return d;
}

struct SandboxTests final : public juce::UnitTest
{
    SandboxTests() : UnitTest ("Plugin isolati", "Sonora") {}

    void runTest() override
    {
        constexpr double rate = 48000.0;
        constexpr int block = 256;

        beginTest ("Il plugin si carica in un processo separato");
        juce::String error;
        auto instance = sandbox::createIsolatedInstance (describe (gainPlugin), rate, block, error, hostExecutable);
        expect (instance != nullptr, error);

        if (instance == nullptr)
            return;

        expect (sandbox::isIsolated (instance.get()));
        expect (! sandbox::hasCrashed (instance.get()));

        beginTest ("Descrizione, bus, latenza e parametri come il plugin vero");
        {
            juce::PluginDescription d;
            instance->fillInPluginDescription (d);
            expectEquals (d.name, juce::String ("Test Gain"));
            expectEquals (d.pluginFormatName, juce::String ("CLAP"));
            expectEquals (instance->getTotalNumInputChannels(), 2);
            expectEquals (instance->getTotalNumOutputChannels(), 2);
            expect (instance->acceptsMidi());

            instance->prepareToPlay (rate, block);
            expectEquals (instance->getLatencySamples(), 16);

            auto& params = instance->getParameters();
            expectEquals (params.size(), 1);

            auto* gain = dynamic_cast<juce::HostedAudioProcessorParameter*> (params[0]);
            expect (gain != nullptr);
            expectEquals (gain->getName (32), juce::String ("Gain"));
            expectEquals (gain->getParameterID(), juce::String ("7"));
            expectWithinAbsoluteError (gain->getValue(), 0.5f, 1.0e-6f);
            expectEquals (gain->getText (0.5f, 32), juce::String ("x1.00"));
        }

        auto processConstant = [&] (float input, juce::MidiBuffer& midi)
        {
            juce::AudioBuffer<float> buffer (2, block);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buffer.setSample (ch, i, input);

            instance->processBlock (buffer, midi);
            return buffer;
        };

        beginTest ("Audio e parametri attraverso i processi");
        {
            juce::MidiBuffer midi;
            auto out = processConstant (0.8f, midi);
            expectWithinAbsoluteError (out.getSample (0, 10), 0.8f, 1.0e-6f);

            auto* gain = instance->getParameters()[0];
            gain->setValueNotifyingHost (0.25f);   // gain 0.5
            out = processConstant (0.8f, midi);
            expectWithinAbsoluteError (out.getSample (0, 10), 0.4f, 1.0e-6f);
            expectWithinAbsoluteError (out.getSample (1, block - 1), 0.4f, 1.0e-6f);

            // Il testo del valore nuovo arriva dal plugin
            expectWithinAbsoluteError (gain->getValue(), 0.25f, 1.0e-6f);

            for (int i = 0; i < 100 && gain->getText (0.25f, 32) != "x0.50"; ++i)
                juce::Thread::sleep (10);

            expectEquals (gain->getText (0.25f, 32), juce::String ("x0.50"));
        }

        beginTest ("Note in ingresso e in uscita");
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 5);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 100);
            processConstant (0.0f, midi);

            int noteOns = 0, noteOffs = 0;

            for (const auto m : midi)
            {
                if (m.getMessage().isNoteOn())  { ++noteOns;  expectEquals (m.samplePosition, 5); expectEquals (m.getMessage().getNoteNumber(), 60); }
                if (m.getMessage().isNoteOff()) { ++noteOffs; expectEquals (m.samplePosition, 100); }
            }

            expectEquals (noteOns, 1);
            expectEquals (noteOffs, 1);
        }

        beginTest ("Blocchi piu' grandi del massimo condiviso");
        {
            juce::AudioBuffer<float> buffer (2, 10000);
            buffer.clear();
            buffer.setSample (0, 9999, 1.0f);
            juce::MidiBuffer midi;
            instance->processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 9999), 0.5f, 1.0e-6f);
        }

        beginTest ("Stato salvato e ripristinato");
        {
            juce::MemoryBlock state;
            instance->getStateInformation (state);
            expect (state.toString().startsWith ("gain=0.5"), state.toString());

            const juce::String newState ("gain=2.0");
            instance->setStateInformation (newState.toRawUTF8(), (int) newState.getNumBytesAsUTF8());

            juce::MidiBuffer midi;
            auto out = processConstant (0.25f, midi);
            expectWithinAbsoluteError (out.getSample (0, 0), 0.5f, 1.0e-6f);

            // Il parametro nella DAW segue lo stato nuovo
            expectWithinAbsoluteError (instance->getParameters()[0]->getValue(), 1.0f, 1.0e-6f);
        }

        beginTest ("Finestra del plugin");
        {
            expect (! instance->hasEditor());
        }

        instance.reset();

        beginTest ("Un plugin che va in crash mentre si carica non porta giu' la DAW");
        {
            juce::String loadError;
            auto crashed = sandbox::createIsolatedInstance (describeWithoutLoading (loadCrashPlugin), rate, block, loadError, hostExecutable);
            expect (crashed == nullptr);
            expect (loadError.isNotEmpty());
            logMessage ("Errore: " + loadError);
        }

        beginTest ("Un plugin che va in crash mentre suona: silenzio e avviso");
        {
            juce::String crashedName;
            sandbox::setCrashCallback ([&] (const juce::String& n) { crashedName = n; });

            juce::String loadError;
            auto fragile = sandbox::createIsolatedInstance (describe (processCrashPlugin), rate, block, loadError, hostExecutable);
            expect (fragile != nullptr, loadError);

            if (fragile != nullptr)
            {
                fragile->prepareToPlay (rate, block);

                juce::AudioBuffer<float> buffer (2, block);
                juce::MidiBuffer midi;
                buffer.clear();
                buffer.setSample (0, 0, 0.5f);
                fragile->processBlock (buffer, midi);
                expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 1.0e-6f);

                // Gain oltre 1,5: il plugin di prova chiama abort()
                fragile->getParameters()[0]->setValueNotifyingHost (0.9f);

                const auto start = juce::Time::getMillisecondCounter();

                for (int i = 0; i < 50 && ! sandbox::hasCrashed (fragile.get()); ++i)
                {
                    buffer.clear();
                    buffer.setSample (0, 0, 0.5f);
                    fragile->processBlock (buffer, midi);
                }

                expect (sandbox::hasCrashed (fragile.get()));
                logMessage ("Crash rilevato in " + juce::String (juce::Time::getMillisecondCounter() - start) + " ms");

                buffer.setSample (0, 0, 0.5f);
                fragile->processBlock (buffer, midi);
                expectEquals (buffer.getMagnitude (0, block), 0.0f);

                // Lo stato resta l'ultimo buono, per non perderlo nel progetto
                juce::MemoryBlock state;
                fragile->getStateInformation (state);
                expect (state.toString().startsWith ("gain="), state.toString());

                juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
                expectEquals (crashedName, juce::String ("Test Crash"));
            }

            sandbox::setCrashCallback (nullptr);
        }

        beginTest ("Scelta dei plugin da isolare");
        {
            auto d = describe (gainPlugin);
            sandbox::setPolicy (sandbox::Policy::chosen);
            expect (! sandbox::shouldIsolate (d));
            sandbox::setChosen (d, true);
            expect (sandbox::shouldIsolate (d));
            sandbox::setPolicy (sandbox::Policy::none);
            expect (! sandbox::shouldIsolate (d));
            sandbox::setPolicy (sandbox::Policy::all);
            sandbox::setChosen (d, false);
            expect (sandbox::shouldIsolate (d));
            sandbox::setPolicy (sandbox::Policy::chosen);
        }
    }
};

SandboxTests sandboxTests;
} // namespace
