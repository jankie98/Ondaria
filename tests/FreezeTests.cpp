/*
    Sonora - test del freeze.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Arrangement.h"
#include "session/Freeze.h"
#include "session/Session.h"
#include "session/Tasks.h"
#include "HostedPlayer.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

using sonora::test::HostedPlayer;
namespace fz = sonora::freeze;

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
            buffer.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / 44100.0f));

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return f;
}

float filePeak (te::Engine& engine, const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));

    if (reader == nullptr)
        return -1.0f;

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
    return buffer.getMagnitude (0, buffer.getNumSamples());
}

struct FreezeTests final : public juce::UnitTest
{
    FreezeTests() : UnitTest ("Freeze", "Sonora") {}

    void runTest() override
    {
        te::Engine engine ("SonoraFreezeTests", std::make_unique<sonora::tasks::HeadlessUIBehaviour>(), nullptr);
        HostedPlayer player (engine);

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sonora-freeze-test");
        dir.deleteRecursively();
        dir.createDirectory();

        auto editFile = dir.getChildFile ("Freeze.tracktionedit");
        auto edit = te::createEmptyEdit (engine, editFile);
        edit->ensureNumberOfAudioTracks (1);
        sonora::session::ensureGrid (*edit);
        sonora::session::setUpFilePaths (*edit);
        te::EditFileOperations (*edit).save (true, true, false);
        edit->getTransport().ensureContextAllocated();
        player.pump();

        auto& track = *te::getAudioTracks (*edit)[0];
        track.setName ("Synth");

        beginTest ("Senza clip non si congela");
        {
            juce::String why;
            expect (! fz::canFreeze (track, &why));
            expect (why.isNotEmpty());
            expect (fz::freeze (track).failed());
        }

        // Un seno di due secondi e un EQ prima del fader
        sonora::arrangement::createClipFromFile (track, 0_bp, writeSine (dir.getChildFile ("seno.wav"), 2.0));
        sonora::arrangement::createClipFromFile (track, 4_bp, writeSine (dir.getChildFile ("seno2.wav"), 1.0));
        auto eq = edit->getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {});
        track.pluginList.insertPlugin (eq, 0, nullptr);
        edit->dispatchPendingUpdatesSynchronously();

        // Picco dell'uscita suonando il primo secondo e mezzo
        auto playAndMeasure = [&]
        {
            auto& transport = edit->getTransport();
            transport.setPosition (0_tp);
            player.process (0.1);
            player.outputPeak = 0.0f;
            transport.play (false);

            // Il file del freeze si legge in sottofondo: diamo un attimo alla cache
            for (int i = 0; i < 15; ++i)
            {
                player.process (0.1);
                player.pump (10);
            }

            transport.stop (false, false);
            player.process (0.1);
            return player.outputPeak;
        };

        beginTest ("Prima del freeze la traccia suona");
        expect (playAndMeasure() > 0.1f);

        beginTest ("Congelare: il file del freeze contiene la traccia, l'EQ si ferma");
        {
            expect (fz::canFreeze (track));
            auto r = fz::freeze (track);
            expect (r.wasOk(), r.getErrorMessage());
            expect (fz::isFrozen (track));
            expect (te::TemporaryFileManager::getFreezeFileForTrack (track).existsAsFile());
            expect (filePeak (engine, te::TemporaryFileManager::getFreezeFileForTrack (track)) > 0.3f);
            expect (eq->isFrozen());
            expect (! fz::canFreeze (track));
        }

        beginTest ("Congelata suona dal file");
        {
            const auto peak = playAndMeasure();
            expect (peak > 0.1f, juce::String (peak));
        }

        beginTest ("Scongelare: torna a suonare con i device vivi");
        {
            fz::unfreeze (track);
            expect (! fz::isFrozen (track));
            expect (! eq->isFrozen());
            expect (playAndMeasure() > 0.1f);
        }

        beginTest ("Ricongelare dopo aver scongelato");
        {
            auto r = fz::freeze (track);
            expect (r.wasOk(), r.getErrorMessage());
            const auto peak = playAndMeasure();
            expect (peak > 0.1f, juce::String (peak));
        }

        beginTest ("Il freeze resta dopo aver salvato e riaperto il progetto");
        {
            te::EditFileOperations (*edit).save (true, true, false);
            auto reopened = sonora::session::loadEdit (engine, editFile);
            expect (reopened != nullptr);

            if (reopened != nullptr)
            {
                auto t = te::getAudioTracks (*reopened)[0];
                expect (t != nullptr && fz::isFrozen (*t));
            }

            reopened.reset();
            fz::unfreeze (track);
        }

        beginTest ("Appiattire: una clip audio al posto di clip e device, poi annullare");
        {
            edit->getUndoManager().beginNewTransaction ("Appiattisci");
            te::Clip* flat = nullptr;
            auto r = fz::flatten (track, &flat);
            expect (r.wasOk(), r.getErrorMessage());
            expect (flat != nullptr);
            expect (! fz::isFrozen (track));
            expectEquals (track.getClips().size(), 1);

            auto wave = dynamic_cast<te::WaveAudioClip*> (track.getClips()[0]);
            expect (wave != nullptr);
            expect (wave != nullptr && wave->getAudioFile().getFile().isAChildOf (dir.getChildFile ("Appiattite")));
            expect (wave != nullptr && filePeak (engine, wave->getAudioFile().getFile()) > 0.3f);

            bool hasEq = false;

            for (auto p : track.pluginList.getPlugins())
                hasEq = hasEq || dynamic_cast<te::EqualiserPlugin*> (p) != nullptr;

            expect (! hasEq);
            expect (track.getVolumePlugin() != nullptr);

            edit->undo();
            edit->dispatchPendingUpdatesSynchronously();
            bool eqBack = false;

            for (auto p : track.pluginList.getPlugins())
                eqBack = eqBack || dynamic_cast<te::EqualiserPlugin*> (p) != nullptr;

            expect (eqBack);
            expectEquals (track.getClips().size(), 2);
            expect (track.getClips().size() == 2 && track.getClips()[0]->getName() == "seno");
        }

        eq = nullptr;
        auto tempDir = edit->getTempDirectory (false);
        edit.reset();
        tempDir.deleteRecursively();
        dir.deleteRecursively();
    }
};

static FreezeTests freezeTests;

} // namespace
