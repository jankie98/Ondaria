/*
    Sonora - test di integrazione: host CLAP dentro Tracktion Engine.
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "host/IsolatedPluginScanner.h"
#include "host/PluginFormats.h"

#include <iostream>

namespace te = tracktion;

namespace
{
const juce::File gainPluginFile    { SONORA_TEST_GAIN_PLUGIN };
const juce::File crashPluginFile   { SONORA_TEST_CRASH_PLUGIN };
const juce::File scannerExecutable { SONORA_SCANNER_PATH };

struct EngineIntegrationTests final : public juce::UnitTest
{
    EngineIntegrationTests() : UnitTest ("Integrazione con il motore", "Sonora") {}

    void runTest() override
    {
        te::Engine engine ("SonoraEngineTests", nullptr, nullptr);

        auto& pm = engine.getPluginManager();
        sonora::addAllPluginFormats (pm.pluginFormatManager);
        pm.knownPluginList.clear();
        pm.knownPluginList.clearBlacklistedFiles();
        pm.knownPluginList.setCustomScanner (std::make_unique<sonora::IsolatedPluginScanner> (scannerExecutable, 30000));

        beginTest ("Scansione di una cartella con un plugin valido e uno che va in crash");

        juce::AudioPluginFormat* clap = nullptr;

        for (auto* f : pm.pluginFormatManager.getFormats())
            if (f->getName() == "CLAP")
                clap = f;

        expect (clap != nullptr);

        if (clap == nullptr)
            return;

        juce::PluginDirectoryScanner scanner (pm.knownPluginList, *clap,
                                              juce::FileSearchPath (gainPluginFile.getParentDirectory().getFullPathName()),
                                              false, {}, false);
        juce::String name;

        while (scanner.scanNextFile (true, name)) {}

        // Nella cartella ci sono Test Gain, la sua variante che va in crash mentre suona
        // (si scansiona senza problemi) e il plugin che va in crash appena caricato
        expectEquals (pm.knownPluginList.getNumTypes(), 2);
        expect (pm.knownPluginList.getBlacklistedFiles().contains (crashPluginFile.getFullPathName()));

        juce::Array<juce::PluginDescription> types;

        for (const auto& t : pm.knownPluginList.getTypes())
            if (t.name == "Test Gain")
                types.add (t);

        expectEquals (types.size(), 1);

        if (types.size() != 1)
            return;

        beginTest ("Il plugin CLAP si inserisce in una traccia e viene caricato");

        auto edit = te::Edit::createSingleTrackEdit (engine);
        auto track = te::getAudioTracks (*edit)[0];
        expect (track != nullptr);

        auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, types.getReference (0));
        expect (plugin != nullptr);
        track->pluginList.insertPlugin (plugin, 0, nullptr);

        auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
        expect (external != nullptr);

        if (external == nullptr)
            return;

        expect (external->getAudioPluginInstance() != nullptr, external->getLoadError());
        expectEquals (external->getName(), juce::String ("Test Gain"));
        expect (external->getAutomatableParameters().size() > 0);

        beginTest ("Lo stato del plugin si salva con il progetto");
        {
            if (auto instance = external->getAudioPluginInstance())
                instance->getParameters()[0]->setValueNotifyingHost (0.25f);

            external->flushPluginStateToValueTree();
            auto state = edit->state.createCopy();

            auto reloaded = te::Edit::createEdit ({ engine, state, edit->getProjectItemRef() });
            auto reloadedTrack = te::getAudioTracks (*reloaded)[0];
            expect (reloadedTrack != nullptr);

            te::ExternalPlugin* reloadedPlugin = nullptr;

            if (reloadedTrack != nullptr)
                for (auto p : reloadedTrack->pluginList.getPlugins())
                    if (auto e = dynamic_cast<te::ExternalPlugin*> (p))
                        reloadedPlugin = e;

            expect (reloadedPlugin != nullptr);

            if (reloadedPlugin != nullptr)
            {
                auto instance = reloadedPlugin->getAudioPluginInstance();
                expect (instance != nullptr);

                if (instance != nullptr)
                    expectWithinAbsoluteError (instance->getParameters()[0]->getValue(), 0.25f, 1.0e-4f);
            }
        }
    }
};

EngineIntegrationTests engineIntegrationTests;
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);

    // Con un argomento gira solo il gruppo con quel nome (es. "Warp")
    if (argc > 1)
    {
        juce::Array<juce::UnitTest*> chosen;

        for (auto* t : juce::UnitTest::getTestsInCategory ("Sonora"))
            if (t->getName() == juce::String (argv[1]))
                chosen.add (t);

        runner.runTests (chosen);
    }
    else
    {
        runner.runTestsInCategory ("Sonora");
    }

    int failures = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    std::cout << (failures == 0 ? "TUTTI I TEST SUPERATI" : "TEST FALLITI: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
