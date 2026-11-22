/*
    Sonora - plugin isolati dentro Tracktion Engine: caricamento, audio, stato,
    cambio d'impostazione e crash mentre il motore suona.
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "app/PluginIsolation.h"
#include "host/ClapPluginFormat.h"
#include "host/PluginFormats.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{
namespace sandbox = sonora::sandbox;
namespace isolation = sonora::isolation;

const juce::File gainFile       { SONORA_TEST_GAIN_PLUGIN };
const juce::File processCrashFile { SONORA_TEST_PROCESS_CRASH_PLUGIN };
const juce::File hostFile       { SONORA_SCANNER_PATH };

juce::PluginDescription describe (const juce::File& file)
{
    sonora::ClapPluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, file.getFullPathName());
    return found.isEmpty() ? juce::PluginDescription() : *found[0];
}

juce::File writeSine (const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
    juce::AudioBuffer<float> buffer (2, 2 * 44100);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            buffer.setSample (ch, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 44100.0f));

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return f;
}

te::ExternalPlugin* findExternal (te::AudioTrack& t)
{
    for (auto p : t.pluginList.getPlugins())
        if (auto e = dynamic_cast<te::ExternalPlugin*> (p))
            return e;

    return nullptr;
}

/** Il parametro del plugin (TE aggiunge Dry e Wet davanti). */
te::AutomatableParameter* gainOf (te::ExternalPlugin& p)
{
    for (auto param : p.getAutomatableParameters())
        if (param->getParameterName() == "Gain")
            return param;

    return nullptr;
}

struct SandboxEngineTests final : public juce::UnitTest
{
    SandboxEngineTests() : UnitTest ("Plugin isolati nel motore", "Sonora") {}

    void runTest() override
    {
        te::Engine engine ("SonoraEngineTests", nullptr, nullptr);
        auto& pm = engine.getPluginManager();
        sonora::addAllPluginFormats (pm.pluginFormatManager);
        isolation::install (pm, hostFile);

        const auto gain = describe (gainFile), fragile = describe (processCrashFile);
        pm.knownPluginList.addType (gain);
        pm.knownPluginList.addType (fragile);

        const auto folder = juce::File::createTempFile ("isolati");
        folder.createDirectory();
        const auto sine = writeSine (folder.getChildFile ("seno.wav"));

        auto edit = te::Edit::createSingleTrackEdit (engine);
        auto& track = *te::getAudioTracks (*edit)[0];
        track.insertWaveClip ("seno", sine, { { 0_tp, 2_tp } }, false);

        int renders = 0;
        auto renderPeak = [&]
        {
            auto out = folder.getChildFile ("render" + juce::String (++renders) + ".wav");
            te::Renderer::renderToFile ("Isolati", out, *edit, { 0_tp, 2_tp }, te::toBitSet (te::getAllTracks (*edit)), true, false, {}, false);

            std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (out));

            if (reader == nullptr)
                return -1.0f;

            const int start = (int) (0.3 * reader->sampleRate), num = (int) (1.2 * reader->sampleRate);
            juce::AudioBuffer<float> buffer ((int) reader->numChannels, num);
            reader->read (&buffer, 0, num, start, true, true);
            return buffer.getMagnitude (0, num);
        };

        beginTest ("Senza plugin");
        const float base = renderPeak();
        expect (base > 0.1f, juce::String (base));

        beginTest ("Con l'impostazione 'tutti' il plugin si carica nel suo processo");
        sandbox::setPolicy (sandbox::Policy::all);

        auto plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, gain);
        track.pluginList.insertPlugin (plugin, 0, nullptr);
        auto external = findExternal (track);
        expect (external != nullptr && external->getAudioPluginInstance() != nullptr,
                external != nullptr ? external->getLoadError() : juce::String());

        if (external == nullptr || external->getAudioPluginInstance() == nullptr)
            return;

        expect (isolation::isIsolated (*external));
        expect (gainOf (*external) != nullptr);

        beginTest ("L'audio passa dal processo del plugin e i parametri di TE lo comandano");
        expectWithinAbsoluteError (renderPeak(), base, base * 0.02f);

        auto param = gainOf (*external);
        expect (param != nullptr);

        if (param == nullptr)
            return;

        param->setNormalisedParameter (0.25f, juce::sendNotification);   // gain 0.5
        expectWithinAbsoluteError (renderPeak(), base * 0.5f, base * 0.02f);

        beginTest ("Lo stato si salva col progetto e alla riapertura resta isolato");
        {
            external->flushPluginStateToValueTree();
            auto reloaded = te::Edit::createEdit ({ engine, edit->state.createCopy(), edit->getProjectItemRef() });
            auto reloadedPlugin = findExternal (*te::getAudioTracks (*reloaded)[0]);
            expect (reloadedPlugin != nullptr);

            if (reloadedPlugin != nullptr)
            {
                expect (isolation::isIsolated (*reloadedPlugin));
                expectWithinAbsoluteError (gainOf (*reloadedPlugin)->getCurrentNormalisedValue(), 0.25f, 1.0e-4f);
            }
        }

        beginTest ("Cambiando impostazione il plugin si ricarica dentro la DAW, con lo stesso stato");
        sandbox::setPolicy (sandbox::Policy::none);
        expectEquals (isolation::applyToEdit (*edit), 1);
        external = findExternal (track);
        expect (external != nullptr && external->getAudioPluginInstance() != nullptr);

        if (external == nullptr || external->getAudioPluginInstance() == nullptr)
            return;

        expect (! isolation::isIsolated (*external));
        expectWithinAbsoluteError (gainOf (*external)->getCurrentNormalisedValue(), 0.25f, 1.0e-4f);
        expectWithinAbsoluteError (renderPeak(), base * 0.5f, base * 0.02f);
        expectEquals (isolation::applyToEdit (*edit), 0);

        beginTest ("Scelto dal menu: solo quel plugin va isolato");
        sandbox::setPolicy (sandbox::Policy::chosen);
        sandbox::setChosen (external->desc, true);
        isolation::reload (*external);
        expect (isolation::isIsolated (*external));
        expectWithinAbsoluteError (renderPeak(), base * 0.5f, base * 0.02f);
        sandbox::setChosen (external->desc, false);
        external->deleteFromParent();

        beginTest ("Un plugin isolato che va in crash nel motore: silenzio, poi si riavvia");
        {
            sandbox::setPolicy (sandbox::Policy::all);
            auto p = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, fragile);
            track.pluginList.insertPlugin (p, 0, nullptr);
            auto crashing = findExternal (track);
            expect (crashing != nullptr && isolation::isIsolated (*crashing));

            if (crashing == nullptr || ! isolation::isIsolated (*crashing))
                return;

            expectWithinAbsoluteError (renderPeak(), base, base * 0.02f);

            // Lo stato buono resta nel progetto: lo salviamo prima di farlo cadere
            crashing->flushPluginStateToValueTree();

            // Gain oltre 1,5: il plugin di prova chiama abort() mentre elabora
            gainOf (*crashing)->setNormalisedParameter (0.9f, juce::sendNotification);
            const auto peak = renderPeak();
            expect (isolation::hasCrashed (*crashing));
            expect (peak < base, juce::String (peak));
            expectEquals (renderPeak(), 0.0f);

            // Riavvio con lo stato prima del crash: torna a suonare
            gainOf (*crashing)->setNormalisedParameter (0.5f, juce::sendNotification);
            expectEquals (isolation::applyToEdit (*edit), 1);
            expect (crashing->getAudioPluginInstance() != nullptr && ! isolation::hasCrashed (*crashing));
            expectWithinAbsoluteError (renderPeak(), base, base * 0.02f);
        }

        sandbox::setPolicy (sandbox::Policy::chosen);
        plugin = nullptr;
        edit.reset();
        folder.deleteRecursively();
    }
};

SandboxEngineTests sandboxEngineTests;
} // namespace
