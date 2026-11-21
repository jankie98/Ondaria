/*
    Sonora - test dei rack di device.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Racks.h"
#include "session/Session.h"
#include "session/Tasks.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

namespace racks = sonora::racks;

juce::File writeSine (const juce::File& f, double seconds)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
    juce::AudioBuffer<float> buffer (2, (int) (seconds * 44100.0));

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            buffer.setSample (ch, i, 0.25f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 44100.0f));

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return f;
}

/** Picco del file tra 0,3 e 1,5 secondi (lontano dalle rampe). */
float middlePeak (te::Engine& engine, const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));

    if (reader == nullptr)
        return -1.0f;

    const int start = (int) (0.3 * reader->sampleRate), num = (int) (1.2 * reader->sampleRate);
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, num);
    reader->read (&buffer, 0, num, start, true, true);
    return buffer.getMagnitude (0, num);
}

/** Valore efficace tra 0,3 e 1,5 secondi: per i sintetizzatori, che hanno fasi libere. */
float middleRms (te::Engine& engine, const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));

    if (reader == nullptr)
        return -1.0f;

    const int start = (int) (0.3 * reader->sampleRate), num = (int) (1.2 * reader->sampleRate);
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, num);
    reader->read (&buffer, 0, num, start, true, true);
    return buffer.getRMSLevel (0, 0, num);
}

te::Plugin::Ptr newVolume (te::Edit& edit, float db)
{
    auto p = edit.getPluginCache().createNewPlugin (te::VolumeAndPanPlugin::xmlTypeName, {});

    if (auto v = dynamic_cast<te::VolumeAndPanPlugin*> (p.get()))
        v->setVolumeDb (db);

    return p;
}

struct RackTests final : public juce::UnitTest
{
    RackTests() : UnitTest ("Rack", "Sonora") {}

    void runTest() override
    {
        te::Engine engine ("SonoraRackTests", std::make_unique<sonora::tasks::HeadlessUIBehaviour>(), nullptr);

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sonora-rack-test");
        dir.deleteRecursively();
        dir.createDirectory();

        auto editFile = dir.getChildFile ("Rack.tracktionedit");
        auto edit = te::createEmptyEdit (engine, editFile);
        edit->ensureNumberOfAudioTracks (2);
        sonora::session::ensureGrid (*edit);
        sonora::session::setUpFilePaths (*edit);
        auto& um = edit->getUndoManager();

        auto& track = *te::getAudioTracks (*edit)[0];
        auto sine = writeSine (dir.getChildFile ("seno.wav"), 2.0);
        track.insertWaveClip ("seno", sine, { { 0_tp, 2_tp } }, false);

        auto render = [&] (const juce::String& name)
        {
            auto out = dir.getChildFile (name + ".wav");
            out.deleteFile();
            edit->dispatchPendingUpdatesSynchronously();
            te::Renderer::renderToFile ("Rack", out, *edit, { 0_tp, 2_tp }, te::toBitSet (te::getAllTracks (*edit)), true, false, {}, false);
            return middlePeak (engine, out);
        };

        // Solo la seconda traccia (il render non guarda il mute delle tracce)
        auto renderRms = [&] (const juce::String& name)
        {
            auto out = dir.getChildFile (name + ".wav");
            out.deleteFile();
            edit->dispatchPendingUpdatesSynchronously();
            juce::Array<te::Track*> only { te::getAudioTracks (*edit)[1] };
            te::Renderer::renderToFile ("Rack", out, *edit, { 0_tp, 2_tp }, te::toBitSet (only), true, false, {}, false);
            return middleRms (engine, out);
        };

        // Il riferimento: la traccia senza rack (col volume della traccia e la sua legge del pan)
        beginTest ("Senza rack");
        const auto base = render ("senza-rack");
        expectGreaterThan (base, 0.1f);

        um.beginNewTransaction();
        auto instance = racks::createRack (track, 0);
        expect (instance != nullptr);

        if (instance == nullptr)
            return;

        auto& rack = *racks::getRackType (*instance);

        beginTest ("Rack vuoto: una catena, il suono passa uguale");
        {
            expect (racks::isRack (*instance));
            expectEquals (track.pluginList.indexOf (instance.get()), 0);
            expectEquals (racks::getNumChains (rack), 1);
            expectEquals (racks::getChainName (rack, 0), juce::String ("Catena 1"));
            expect (racks::getChainFader (rack, 0) != nullptr);
            expectEquals (racks::getMacros (rack).size(), racks::numMacros);
            expectWithinAbsoluteError (render ("una-catena"), base, 0.01f);
        }

        beginTest ("Catene in parallelo si sommano");
        {
            um.beginNewTransaction();
            expectEquals (racks::addChain (rack), 1);
            expectEquals (racks::getChainName (rack, 1), juce::String ("Catena 2"));
            expectWithinAbsoluteError (render ("due-catene"), 2.0f * base, 0.01f);

            um.beginNewTransaction();
            racks::getChainFader (rack, 1)->setVolumeDb (-6.0206f);
            expectWithinAbsoluteError (render ("catena-a-meta"), 1.5f * base, 0.01f);
            racks::getChainFader (rack, 1)->setVolumeDb (0.0f);
        }

        beginTest ("Mute e solo delle catene");
        {
            um.beginNewTransaction();
            racks::setChainMuted (rack, 1, true);
            expect (! racks::isChainAudible (rack, 1));
            expectWithinAbsoluteError (render ("muta"), base, 0.01f);
            racks::setChainMuted (rack, 1, false);

            racks::setChainSoloed (rack, 0, true);
            expect (racks::isChainAudible (rack, 0));
            expect (! racks::isChainAudible (rack, 1));
            expectWithinAbsoluteError (render ("solo"), base, 0.01f);
            racks::setChainSoloed (rack, 0, false);
            expectWithinAbsoluteError (render ("di-nuovo-due"), 2.0f * base, 0.01f);
        }

        beginTest ("Device dentro una catena");
        {
            um.beginNewTransaction();
            auto silent = newVolume (*edit, -100.0f);
            expect (racks::insertPlugin (rack, 0, silent));
            expectEquals (racks::getChainPlugins (rack, 0).size(), 1);
            expectEquals (racks::findChainOf (rack, *silent), 0);
            expect (racks::getContainingRack (*silent) == &rack);
            expectWithinAbsoluteError (render ("catena-zitta"), base, 0.01f);

            // Un secondo device davanti: l'ordine conta e si rispetta
            auto louder = newVolume (*edit, 6.0206f);
            expect (racks::insertPlugin (rack, 1, louder, 0));
            expectWithinAbsoluteError (render ("catena-piu-forte"), 2.0f * base, 0.02f);

            // Spostato nell'altra catena
            expect (racks::insertPlugin (rack, 0, louder));
            expectEquals (racks::getChainPlugins (rack, 1).size(), 0);
            expect (racks::getChainPlugins (rack, 0).getLast() == louder.get());

            racks::removePlugin (rack, *louder);
            racks::removePlugin (rack, *silent);
            expectEquals (racks::getChainPlugins (rack, 0).size(), 0);
            expectWithinAbsoluteError (render ("di-nuovo-vuote"), 2.0f * base, 0.01f);
        }

        beginTest ("Undo di una catena");
        {
            um.beginNewTransaction();
            racks::addChain (rack);
            expectEquals (racks::getNumChains (rack), 3);
            edit->undo();
            expectEquals (racks::getNumChains (rack), 2);
            expectWithinAbsoluteError (render ("dopo-undo"), 2.0f * base, 0.01f);
        }

        beginTest ("Macro sui parametri dei device");
        {
            um.beginNewTransaction();
            auto device = newVolume (*edit, 0.0f);
            racks::insertPlugin (rack, 0, device);
            auto vol = dynamic_cast<te::VolumeAndPanPlugin*> (device.get())->volParam;

            racks::setMacroValue (rack, 2, 0.0f);
            expect (racks::map (rack, 2, *vol));
            expectEquals (racks::getMacroFor (rack, *vol), 2);
            expectEquals ((int) racks::getMappings (rack, 2).size(), 1);
            expectWithinAbsoluteError (vol->getCurrentNormalisedValue(), 0.0f, 0.001f);

            racks::setMacroValue (rack, 2, 1.0f);
            expectWithinAbsoluteError (vol->getCurrentNormalisedValue(), 1.0f, 0.001f);

            racks::setMappingRange (rack, 2, *vol, 0.2f, 0.6f);
            racks::setMacroValue (rack, 2, 0.5f);
            expectWithinAbsoluteError (vol->getCurrentNormalisedValue(), 0.4f, 0.001f);

            auto mapping = racks::getMappings (rack, 2).front();
            expectWithinAbsoluteError (mapping.minimum, 0.2f, 0.001f);
            expectWithinAbsoluteError (mapping.maximum, 0.6f, 0.001f);

            // Al contrario
            racks::setMappingRange (rack, 2, *vol, 0.9f, 0.1f);
            racks::setMacroValue (rack, 2, 0.25f);
            expectWithinAbsoluteError (vol->getCurrentNormalisedValue(), 0.7f, 0.001f);

            // Un parametro va su una macro sola
            expect (racks::map (rack, 5, *vol));
            expectEquals (racks::getMacroFor (rack, *vol), 5);
            expect (racks::getMappings (rack, 2).empty());

            // I parametri fuori dal rack non si possono collegare
            auto outside = dynamic_cast<te::VolumeAndPanPlugin*> (track.pluginList.getPluginsOfType<te::VolumeAndPanPlugin>().getLast());
            expect (outside != nullptr && ! racks::map (rack, 0, *outside->volParam));

            racks::setMacroValue (rack, 5, 0.5f);
            const auto before = vol->getCurrentNormalisedValue();
            racks::unmap (rack, 5, *vol);
            expectEquals (racks::getMacroFor (rack, *vol), -1);
            expectWithinAbsoluteError (vol->getCurrentNormalisedValue(), before, 0.001f);

            racks::map (rack, 0, *vol);
            racks::setMacroName (rack, 0, "Volume");
            racks::setMacroValue (rack, 0, 0.75f);
        }

        beginTest ("Catena duplicata");
        {
            um.beginNewTransaction();
            racks::setChainName (rack, 0, "Pulita");
            expectEquals (racks::duplicateChain (rack, 0), 1);
            expectEquals (racks::getNumChains (rack), 3);
            expectEquals (racks::getChainName (rack, 1), juce::String ("Pulita (copia)"));
            auto original = racks::getChainPlugins (rack, 0), copy = racks::getChainPlugins (rack, 1);
            expectEquals (copy.size(), original.size());
            expect (copy.getFirst() != original.getFirst());
            expect (copy.getFirst()->itemID != original.getFirst()->itemID);
            expectEquals (racks::getSelectedChain (rack), 1);
            racks::removeChain (rack, 1);
            expectEquals (racks::getNumChains (rack), 2);
        }

        beginTest ("Salvataggio e riapertura");
        {
            te::EditFileOperations (*edit).save (true, true, false);
            auto reloaded = sonora::session::loadEdit (engine, editFile);
            expect (reloaded != nullptr);

            if (reloaded != nullptr)
            {
                auto& t = *te::getAudioTracks (*reloaded)[0];
                auto first = t.pluginList[0];
                expect (first != nullptr && racks::isRack (*first));

                if (first != nullptr && racks::isRack (*first))
                {
                    auto& r = *racks::getRackType (*first);
                    expectEquals (racks::getNumChains (r), 2);
                    expectEquals (racks::getChainName (r, 0), juce::String ("Pulita"));
                    expectEquals (racks::getChainPlugins (r, 0).size(), 1);
                    expectEquals (racks::getMacroName (r, 0), juce::String ("Volume"));
                    expectWithinAbsoluteError (racks::getMacroValue (r, 0), 0.75f, 0.001f);
                    expectEquals ((int) racks::getMappings (r, 0).size(), 1);
                }
            }
        }

        beginTest ("Raggruppa e separa");
        {
            auto& other = *te::getAudioTracks (*edit)[1];
            auto eq = edit->getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {});
            auto comp = edit->getPluginCache().createNewPlugin (te::CompressorPlugin::xmlTypeName, {});
            other.pluginList.insertPlugin (eq, 0, nullptr);
            other.pluginList.insertPlugin (comp, 1, nullptr);
            const int racksBefore = edit->getRackList().size();

            um.beginNewTransaction();
            te::Plugin::Array selection;
            selection.add (comp);
            selection.add (eq);
            auto grouped = racks::group (other, selection);
            expect (grouped != nullptr);
            expectEquals (edit->getRackList().size(), racksBefore + 1);
            expect (other.pluginList[0] == grouped.get());
            expect (other.pluginList.indexOf (eq.get()) < 0);

            auto& r = *racks::getRackType (*grouped);
            auto inside = racks::getChainPlugins (r, 0);
            expectEquals (inside.size(), 2);
            expect (inside[0] == eq.get() && inside[1] == comp.get());

            expect (racks::canUngroup (*grouped));
            um.beginNewTransaction();
            expect (racks::ungroup (*grouped));
            expect (other.pluginList[0] == eq.get() && other.pluginList[1] == comp.get());
            expect (! racks::isRack (*other.pluginList[2]));

            // Undo: il rack torna con i suoi device
            edit->undo();
            expect (racks::isRack (*other.pluginList[0]));
            expectEquals (racks::getChainPlugins (*racks::getRackType (*other.pluginList[0]), 0).size(), 2);

            um.beginNewTransaction();
            racks::addChain (*racks::getRackType (*other.pluginList[0]));
            expect (! racks::canUngroup (*other.pluginList[0]));

            um.beginNewTransaction();
            racks::deleteRack (*other.pluginList[0]);
            expect (other.pluginList.getPluginsOfType<te::EqualiserPlugin>().isEmpty());
            expect (other.pluginList.getPluginsOfType<te::RackInstance>().isEmpty());

            // I rack rimasti senza traccia spariscono alla riapertura
            te::EditFileOperations (*edit).save (true, true, false);
            auto reloaded = sonora::session::loadEdit (engine, editFile);
            expectEquals (reloaded->getRackList().size(), 1);
        }

        beginTest ("Rack di strumenti: una catena per suono");
        {
            auto& synthTrack = *te::getAudioTracks (*edit)[1];
            auto midi = dynamic_cast<te::MidiClip*> (synthTrack.insertNewClip (te::TrackItem::Type::midi, "note", { 0_tp, 2_tp }, nullptr));
            expect (midi != nullptr);

            if (midi != nullptr)
                midi->getSequence().addNote (57, 0_bp, 4_bd, 100, 0, nullptr);

            auto newSynth = [&] { return edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {}); };

            um.beginNewTransaction();
            auto synthRack = racks::createRack (synthTrack, 0, "Strati");
            auto& r = *racks::getRackType (*synthRack);
            racks::insertPlugin (r, 0, newSynth());
            const auto one = renderRms ("uno-strumento");
            expectGreaterThan (one, 0.02f);

            racks::insertPlugin (r, racks::addChain (r), newSynth());
            // Due 4OSC uguali ma con fasi libere: la somma cambia da un render all'altro,
            // quindi si controlla ogni strato da solo (la somma la provano le catene audio).
            expectGreaterThan (renderRms ("due-strumenti"), 0.02f);

            racks::setChainSoloed (r, 0, true);
            expectWithinAbsoluteError (renderRms ("primo-strato"), one, 0.05f * one);
            racks::setChainSoloed (r, 0, false);

            racks::setChainMuted (r, 0, true);
            expectWithinAbsoluteError (renderRms ("un-solo-strato"), one, 0.05f * one);

            racks::setChainMuted (r, 1, true);
            expectLessThan (renderRms ("tutto-muto"), 0.001f);
        }

        dir.deleteRecursively();
    }
};

static RackTests rackTests;

} // namespace
