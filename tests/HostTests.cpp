/*
    Sonora - test automatici dell'host dei plugin.
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/ClapPluginFormat.h"
#include "host/IsolatedPluginScanner.h"
#include "host/PluginFormats.h"

#include <iostream>

namespace
{
const juce::File gainPluginFile    { SONORA_TEST_GAIN_PLUGIN };
const juce::File crashPluginFile   { SONORA_TEST_CRASH_PLUGIN };
const juce::File scannerExecutable { SONORA_SCANNER_PATH };

//==============================================================================
struct ClapFormatTests final : public juce::UnitTest
{
    ClapFormatTests() : UnitTest ("Formato CLAP", "Sonora") {}

    std::unique_ptr<juce::AudioPluginInstance> load (sonora::ClapPluginFormat& format, double rate, int block)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        format.findAllTypesForFile (found, gainPluginFile.getFullPathName());

        if (found.size() != 1)
            return {};

        juce::String error;
        auto instance = format.createInstanceFromDescription (*found[0], rate, block, error);
        expect (error.isEmpty(), error);
        return instance;
    }

    void runTest() override
    {
        sonora::ClapPluginFormat format;

        beginTest ("Descrizione del plugin");
        {
            expect (format.fileMightContainThisPluginType (gainPluginFile.getFullPathName()));
            expect (! format.fileMightContainThisPluginType ("/tmp/non-un-plugin.so"));

            juce::OwnedArray<juce::PluginDescription> found;
            format.findAllTypesForFile (found, gainPluginFile.getFullPathName());
            expectEquals (found.size(), 1);

            auto& d = *found[0];
            expectEquals (d.name, juce::String ("Test Gain"));
            expectEquals (d.pluginFormatName, juce::String ("CLAP"));
            expectEquals (d.manufacturerName, juce::String ("Sonora"));
            expectEquals (d.version, juce::String ("1.0.0"));
            expectEquals (d.uniqueId, sonora::ClapPluginFormat::uniqueIdForClapId ("org.sonora.test-gain"));
            expectEquals (d.numInputChannels, 2);
            expectEquals (d.numOutputChannels, 2);
            expect (! d.isInstrument);
            expect (d.category.startsWith ("Effect"), d.category);
            expect (! format.pluginNeedsRescanning (d));
        }

        beginTest ("Ricerca nelle cartelle");
        {
            juce::FileSearchPath path (gainPluginFile.getParentDirectory().getFullPathName());
            auto files = format.searchPathsForPlugins (path, false);
            expect (files.contains (gainPluginFile.getFullPathName()), files.joinIntoString (", "));
        }

        constexpr double rate = 48000.0;
        constexpr int block = 256;
        auto instance = load (format, rate, block);
        expect (instance != nullptr, "istanza non creata");

        if (instance == nullptr)
            return;

        beginTest ("Bus, latenza e parametri");
        {
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
            expectEquals (gain->getText (0.25f, 32), juce::String ("x0.50"));
            expectWithinAbsoluteError (gain->getValueForText ("x1.5"), 0.75f, 1.0e-6f);
        }

        beginTest ("Elaborazione audio con il parametro");
        {
            auto* gain = instance->getParameters()[0];
            gain->setValueNotifyingHost (0.25f);   // gain 0.5

            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buffer.setSample (ch, i, 0.8f);

            instance->processBlock (buffer, midi);

            expectWithinAbsoluteError (buffer.getSample (0, 10), 0.4f, 1.0e-6f);
            expectWithinAbsoluteError (buffer.getSample (1, block - 1), 0.4f, 1.0e-6f);
        }

        beginTest ("Note in ingresso e in uscita");
        {
            juce::AudioBuffer<float> buffer (2, block);
            buffer.clear();
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 5);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 100);

            instance->processBlock (buffer, midi);

            int noteOns = 0, noteOffs = 0;

            for (const auto m : midi)
            {
                if (m.getMessage().isNoteOn())  { ++noteOns;  expectEquals (m.samplePosition, 5); expectEquals (m.getMessage().getNoteNumber(), 60); }
                if (m.getMessage().isNoteOff()) { ++noteOffs; expectEquals (m.samplePosition, 100); }
            }

            expectEquals (noteOns, 1);
            expectEquals (noteOffs, 1);
        }

        beginTest ("Salvataggio e ripristino dello stato");
        {
            juce::MemoryBlock state;
            instance->getStateInformation (state);
            expect (state.getSize() > 0);

            auto second = load (format, rate, block);
            expect (second != nullptr);

            if (second != nullptr)
            {
                expectWithinAbsoluteError (second->getParameters()[0]->getValue(), 0.5f, 1.0e-6f);
                second->setStateInformation (state.getData(), (int) state.getSize());
                expectWithinAbsoluteError (second->getParameters()[0]->getValue(), 0.25f, 1.0e-6f);
            }
        }

        beginTest ("Parametro cambiato a plugin fermo");
        {
            instance->releaseResources();
            instance->getParameters()[0]->setValueNotifyingHost (1.0f);   // gain 2, inviato con flush()
            instance->prepareToPlay (rate, block);

            juce::AudioBuffer<float> buffer (2, block);
            juce::MidiBuffer midi;
            buffer.clear();
            buffer.setSample (0, 0, 0.25f);
            instance->processBlock (buffer, midi);
            expectWithinAbsoluteError (buffer.getSample (0, 0), 0.5f, 1.0e-6f);
        }

        instance->releaseResources();
    }
};

//==============================================================================
struct IsolatedScannerTests final : public juce::UnitTest
{
    IsolatedScannerTests() : UnitTest ("Scansione isolata", "Sonora") {}

    void runTest() override
    {
        sonora::IsolatedPluginScanner scanner (scannerExecutable, 30000);

        beginTest ("Formati disponibili");
        {
            auto names = sonora::getSupportedFormatNames();
            expect (names.contains ("CLAP"), names.joinIntoString (", "));
            expect (names.contains ("VST3"), names.joinIntoString (", "));
            expect (names.contains ("LV2"), names.joinIntoString (", "));
        }

        beginTest ("Plugin valido scansionato in un processo separato");
        {
            juce::OwnedArray<juce::PluginDescription> found;
            auto outcome = scanner.scanFile ("CLAP", gainPluginFile.getFullPathName(), found);
            expect (outcome == sonora::IsolatedPluginScanner::Outcome::found);
            expectEquals (found.size(), 1);

            if (found.size() == 1)
                expectEquals (found[0]->name, juce::String ("Test Gain"));
        }

        beginTest ("Plugin che va in crash non ferma l'host");
        {
            juce::OwnedArray<juce::PluginDescription> found;
            auto outcome = scanner.scanFile ("CLAP", crashPluginFile.getFullPathName(), found);
            expect (outcome == sonora::IsolatedPluginScanner::Outcome::crashed
                     || outcome == sonora::IsolatedPluginScanner::Outcome::timedOut);
            expectEquals (found.size(), 0);
        }

        beginTest ("KnownPluginList: crash in lista di esclusione, plugin valido in lista");
        {
            juce::AudioPluginFormatManager manager;
            sonora::addAllPluginFormats (manager);

            juce::KnownPluginList list;
            list.setCustomScanner (std::make_unique<sonora::IsolatedPluginScanner> (scannerExecutable, 30000));

            juce::AudioPluginFormat* clap = nullptr;

            for (auto* f : manager.getFormats())
                if (f->getName() == "CLAP")
                    clap = f;

            expect (clap != nullptr);

            if (clap != nullptr)
            {
                juce::OwnedArray<juce::PluginDescription> found;
                list.scanAndAddFile (gainPluginFile.getFullPathName(), true, found, *clap);
                list.scanAndAddFile (crashPluginFile.getFullPathName(), true, found, *clap);

                expectEquals (list.getNumTypes(), 1);
                expect (list.getBlacklistedFiles().contains (crashPluginFile.getFullPathName()));
                expect (! list.getBlacklistedFiles().contains (gainPluginFile.getFullPathName()));
            }
        }
    }
};

ClapFormatTests clapFormatTests;
IsolatedScannerTests isolatedScannerTests;
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("Sonora");

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
