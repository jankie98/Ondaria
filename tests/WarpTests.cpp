/*
    Sonora - test del warp audio.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Arrangement.h"
#include "session/Session.h"
#include "session/Tasks.h"
#include "session/Warp.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

namespace warp = sonora::warp;

/** Clic (brevi raffiche a 1 kHz) ogni mezzo secondo: un loop a 120 BPM. */
juce::File writeClicks (const juce::File& f, double seconds, double interval)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
    juce::AudioBuffer<float> buffer (2, (int) (seconds * 44100.0));
    buffer.clear();
    const int burst = (int) (0.015 * 44100.0);

    for (double t = 0.0; t < seconds - 0.02; t += interval)
    {
        const int start = (int) (t * 44100.0);

        for (int i = 0; i < burst; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, start + i, 0.8f * std::sin (juce::MathConstants<float>::twoPi * 1000.0f * (float) i / 44100.0f));
    }

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return f;
}

/** Gli attacchi nel file: dove il livello sale dopo almeno 60 ms di quiete. */
juce::Array<double> onsets (te::Engine& engine, const juce::File& f)
{
    juce::Array<double> result;
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));

    if (reader == nullptr)
        return result;

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

    const int window = (int) (0.002 * reader->sampleRate);
    const int quietWindows = 30;
    int quiet = quietWindows;

    for (int start = 0; start + window < buffer.getNumSamples(); start += window)
    {
        const float level = buffer.getMagnitude (0, start, window);

        if (level > 0.15f)
        {
            if (quiet >= quietWindows)
                result.add (start / reader->sampleRate);

            quiet = 0;
        }
        else if (level < 0.05f)
        {
            ++quiet;
        }
    }

    return result;
}

struct WarpTests final : public juce::UnitTest
{
    WarpTests() : UnitTest ("Warp", "Sonora") {}

    void expectOnsets (const juce::Array<double>& got, std::initializer_list<double> wanted, double tolerance = 0.05)
    {
        juce::String gotText;

        for (auto t : got)
            gotText << juce::String (t, 3) << " ";

        expectEquals (got.size(), (int) wanted.size(), "attacchi: " + gotText);

        int i = 0;

        for (auto w : wanted)
        {
            if (i < got.size())
                expect (std::abs (got[i] - w) < tolerance, "atteso " + juce::String (w) + ", trovato: " + gotText);

            ++i;
        }
    }

    void runTest() override
    {
        beginTest ("Tempo stimato dalla durata e dal rilevatore");
        {
            expectWithinAbsoluteError (warp::guessTempo (2.0, 0.0, 4, 133.0), 120.0, 0.01);       // una battuta a 120
            expectWithinAbsoluteError (warp::guessTempo (32.0 * 60.0 / 95.0, 0.0, 4, 120.0), 95.0, 0.01);
            expectWithinAbsoluteError (warp::guessTempo (32.0 * 60.0 / 95.0, 94.6, 4, 120.0), 95.0, 0.01);
            expectWithinAbsoluteError (warp::guessTempo (180.0, 100.0, 4, 120.0), 100.0, 0.01);    // brano intero
            expectWithinAbsoluteError (warp::guessTempo (180.0, 50.0, 4, 120.0), 100.0, 0.01);     // mezzo tempo
            expectWithinAbsoluteError (warp::guessTempo (180.0, 0.0, 4, 128.0), 128.0, 0.01);      // non si sa
            expectWithinAbsoluteError (warp::guessTempo (2.0, 87.0, 4, 120.0), 87.0, 0.01);        // non e' un loop
        }

        te::Engine engine ("SonoraWarpTests", std::make_unique<sonora::tasks::HeadlessUIBehaviour>(), nullptr);

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sonora-warp-test");
        dir.deleteRecursively();
        dir.createDirectory();

        auto edit = te::createEmptyEdit (engine, dir.getChildFile ("Warp.tracktionedit"));
        edit->ensureNumberOfAudioTracks (1);
        sonora::session::ensureGrid (*edit);
        sonora::session::setUpFilePaths (*edit);
        edit->tempoSequence.getTempos()[0]->setBpm (60.0);

        auto& track = *te::getAudioTracks (*edit)[0];
        const auto loop = writeClicks (dir.getChildFile ("clic.wav"), 4.0, 0.5);

        beginTest ("Il rilevatore trova il tempo dei clic");
        {
            const auto bpm = warp::detectTempo (engine, loop);
            expectWithinAbsoluteError (bpm, 120.0, 2.0);
            expectWithinAbsoluteError (warp::detectTempo (engine, writeClicks (dir.getChildFile ("clic-97.wav"), 12.0, 60.0 / 97.0)), 97.0, 2.0);
        }

        auto clip = dynamic_cast<te::WaveAudioClip*> (sonora::arrangement::createClipFromFile (track, 0_bp, loop));
        expect (clip != nullptr);

        if (clip == nullptr)
            return;

        auto render = [&] (const juce::String& name)
        {
            auto out = dir.getChildFile (name + ".wav");
            out.deleteFile();
            edit->dispatchPendingUpdatesSynchronously();
            te::Renderer::renderToFile ("Warp", out, *edit, { 0_tp, 9_tp }, te::toBitSet (te::getAllTracks (*edit)), true, false, {}, false);
            return out;
        };

        beginTest ("Import: tempo stimato e warp acceso");
        {
            warp::prepareImported (*clip);
            expect (warp::isWarped (*clip));
            expect (warp::getMode (*clip) == warp::Mode::complete);
            expectWithinAbsoluteError (warp::getOriginalTempo (*clip), 120.0, 0.01);
            // Otto beat a 120 sono otto secondi a 60
            expectWithinAbsoluteError (clip->getPosition().getLength().inSeconds(), 8.0, 0.01);
        }

        auto sourceAt = [&] (double seconds)
        {
            return warp::sourceSecondsAt (*clip, te::TimePosition::fromSeconds (seconds)).value_or (-1.0);
        };

        beginTest ("Punto del file sotto il tempo del progetto");
        {
            expectWithinAbsoluteError (sourceAt (0.0), 0.0, 1.0e-6);
            expectWithinAbsoluteError (sourceAt (2.0), 1.0, 1.0e-6);
            expect (! warp::sourceSecondsAt (*clip, te::TimePosition::fromSeconds (8.5)).has_value());
        }

        beginTest ("Con il warp i clic cadono sui beat del progetto");
        expectOnsets (onsets (engine, render ("warp")), { 0, 1, 2, 3, 4, 5, 6, 7 });

        beginTest ("Ripitch: stessa griglia, velocita' cambiata");
        {
            warp::setMode (*clip, warp::Mode::repitch);
            expect (warp::isWarped (*clip));
            expect (! clip->getAutoTempo());
            expectWithinAbsoluteError (clip->getSpeedRatio(), 0.5, 1.0e-6);
            expectWithinAbsoluteError (sourceAt (2.0), 1.0, 1.0e-6);
            expectOnsets (onsets (engine, render ("ripitch")), { 0, 1, 2, 3, 4, 5, 6, 7 });
        }

        beginTest ("Ripitch segue il tempo del progetto");
        {
            edit->tempoSequence.getTempos()[0]->setBpm (120.0);
            expectEquals (warp::followTempo (*edit), 1);
            expectWithinAbsoluteError (clip->getSpeedRatio(), 1.0, 1.0e-6);
            expectWithinAbsoluteError (clip->getPosition().getLength().inSeconds(), 4.0, 0.01);
            expectEquals (warp::followTempo (*edit), 0);
            edit->tempoSequence.getTempos()[0]->setBpm (60.0);
            warp::followTempo (*edit);
            warp::setMode (*clip, warp::Mode::complete);
            expect (clip->getAutoTempo());
        }

        beginTest ("Senza warp la clip suona alla velocita' originale");
        {
            warp::setWarped (*clip, false);
            expect (! warp::isWarped (*clip));
            expect (! clip->getAutoTempo());
            expectWithinAbsoluteError (sourceAt (2.0), 2.0, 1.0e-6);
            expectOnsets (onsets (engine, render ("senza-warp")), { 0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5 });
            warp::setWarped (*clip, true);
            expect (clip->getAutoTempo());
        }

        beginTest ("Tempo originale dimezzato: la clip occupa meta' delle battute");
        {
            warp::setOriginalTempo (*clip, 60.0);
            expectWithinAbsoluteError (warp::getOriginalTempo (*clip), 60.0, 0.01);
            expectWithinAbsoluteError (clip->getPosition().getLength().inSeconds(), 4.0, 0.01);
            expectOnsets (onsets (engine, render ("tempo-60")), { 0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5 });
            warp::setOriginalTempo (*clip, 120.0);
            expectWithinAbsoluteError (clip->getPosition().getLength().inSeconds(), 8.0, 0.01);
        }

        beginTest ("Trasposizione e accordatura fine");
        {
            warp::setTranspose (*clip, -5);
            warp::setDetune (*clip, 30);
            expectEquals (warp::getTranspose (*clip), -5);
            expectEquals (warp::getDetune (*clip), 30);
            warp::setTranspose (*clip, 0);
            warp::setDetune (*clip, 0);
            expectEquals (clip->getPitchChange(), 0.0f);
        }

        beginTest ("Marker di warp: spostarne uno allunga solo il tratto prima");
        {
            expect (warp::getMarkers (*clip).empty());
            const int index = warp::addMarker (*clip, 2.0);
            expect (index > 0);
            auto markers = warp::getMarkers (*clip);
            expectEquals ((int) markers.size(), 3);
            expectWithinAbsoluteError (markers[1].beat, 4.0, 0.001);

            // Il quinto clic (2 s nel file, beat 4) va al beat 6: prima si allarga, dopo si stringe
            warp::moveMarker (*clip, index, 6.0);
            markers = warp::getMarkers (*clip);
            expectWithinAbsoluteError (markers[1].beat, 6.0, 0.001);
            expectWithinAbsoluteError (sourceAt (6.0), 2.0, 0.001);
            expectWithinAbsoluteError (sourceAt (3.0), 1.0, 0.001);
            expectWithinAbsoluteError (sourceAt (7.0), 3.0, 0.001);
            expectOnsets (onsets (engine, render ("marker")), { 0, 1.5, 3, 4.5, 6, 6.5, 7, 7.5 }, 0.06);

            // Non puo' scavalcare i vicini
            warp::moveMarker (*clip, index, 20.0);
            expect (warp::getMarkers (*clip)[1].beat < 8.0);

            warp::removeMarker (*clip, 0);
            expectEquals ((int) warp::getMarkers (*clip).size(), 3);
            warp::resetMarkers (*clip);
            expect (warp::getMarkers (*clip).empty());
            expectOnsets (onsets (engine, render ("marker-tolti")), { 0, 1, 2, 3, 4, 5, 6, 7 });
        }

        beginTest ("Il warp resta salvando e riaprendo");
        {
            warp::setOriginalTempo (*clip, 96.0);
            warp::setMode (*clip, warp::Mode::light);
            auto file = dir.getChildFile ("Warp.tracktionedit");
            te::EditFileOperations (*edit).save (true, true, false);
            auto copy = sonora::session::loadEdit (engine, file);
            sonora::session::setUpFilePaths (*copy);
            auto reloaded = dynamic_cast<te::WaveAudioClip*> (te::getAudioTracks (*copy)[0]->getClips()[0]);
            expect (reloaded != nullptr);

            if (reloaded != nullptr)
            {
                expect (warp::isWarped (*reloaded));
                expect (warp::getMode (*reloaded) == warp::Mode::light);
                expectWithinAbsoluteError (warp::getOriginalTempo (*reloaded), 96.0, 0.01);
            }
        }

        auto tempDir = edit->getTempDirectory (false);
        edit.reset();
        tempDir.deleteRecursively();
        dir.deleteRecursively();
    }
};

static WarpTests warpTests;

} // namespace
