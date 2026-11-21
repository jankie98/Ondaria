/*
    Sonora - test dell'export.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Arrangement.h"
#include "session/Export.h"
#include "session/Session.h"
#include "HostedPlayer.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

using sonora::test::HostedPlayer;
namespace ex = sonora::exporting;

/** Scrive un file con un seno di un secondo. */
juce::File writeSine (const juce::File& dir, const juce::String& name, float frequency)
{
    auto f = dir.getChildFile (name + ".wav");
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = std::unique_ptr<juce::OutputStream> (f.createOutputStream().release());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (44100.0)
                                                                              .withNumChannels (2)
                                                                              .withBitsPerSample (24));
    juce::AudioBuffer<float> buffer (2, 44100);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            buffer.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * frequency * (float) i / 44100.0f));

    writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    return f;
}

struct FileInfo
{
    bool ok = false;
    double seconds = 0.0;
    double sampleRate = 0.0;
    int bits = 0;
    float peak = 0.0f;
    float peakIn (double from, double to) const
    {
        const int a = juce::jlimit (0, buffer.getNumSamples(), (int) (from * sampleRate));
        const int b = juce::jlimit (0, buffer.getNumSamples(), (int) (to * sampleRate));
        return b > a ? buffer.getMagnitude (0, a, b - a) : 0.0f;
    }

    juce::AudioBuffer<float> buffer;
};

FileInfo readFile (te::Engine& engine, const juce::File& f)
{
    FileInfo info;
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f));

    if (reader == nullptr)
        return info;

    info.ok = true;
    info.sampleRate = reader->sampleRate;
    info.bits = (int) reader->bitsPerSample;
    info.seconds = (double) reader->lengthInSamples / reader->sampleRate;
    info.buffer.setSize ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&info.buffer, 0, (int) reader->lengthInSamples, 0, true, true);
    info.peak = info.buffer.getMagnitude (0, info.buffer.getNumSamples());
    return info;
}

struct ExportTests final : public juce::UnitTest
{
    ExportTests() : UnitTest ("Export", "Sonora") {}

    /** Avvia l'export e aspetta che finisca. */
    juce::Result run (te::Edit& edit, const ex::Settings& s, juce::Array<juce::File>& files, HostedPlayer& player)
    {
        ex::Exporter exporter;
        std::optional<juce::Result> result;
        exporter.onFinished = [&] (juce::Result r, juce::Array<juce::File> f)
        {
            result = r;
            files = f;
        };

        if (auto r = exporter.start (edit, s); r.failed())
            return r;

        expect (exporter.isRunning());

        for (int i = 0; i < 600 && ! result; ++i)
            player.pump (50);

        expect (result.has_value(), "l'export non e' finito");
        return result.value_or (juce::Result::fail ("timeout"));
    }

    void runTest() override
    {
        te::Engine engine ("SonoraExportTests", nullptr, nullptr);
        HostedPlayer player (engine);

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("sonora-export-test");
        dir.deleteRecursively();
        dir.createDirectory();

        auto edit = te::createEmptyEdit (engine, dir.getChildFile ("Brano di prova.tracktionedit"));
        edit->ensureNumberOfAudioTracks (3);
        sonora::session::ensureGrid (*edit);
        sonora::session::setUpFilePaths (*edit);
        edit->tempoSequence.getTempos()[0]->setBpm (120.0);
        edit->getTransport().ensureContextAllocated();
        edit->dispatchPendingUpdatesSynchronously();
        player.pump();

        auto tracks = te::getAudioTracks (*edit);
        tracks[0]->setName ("Basso");
        tracks[1]->setName ("Lead");
        tracks[2]->setName ("Vuota");

        beginTest ("Senza clip non c'e' niente da esportare");
        {
            ex::Settings s;
            s.destination = dir.getChildFile ("vuoto.wav");
            juce::Array<juce::File> files;
            expect (run (*edit, s, files, player).failed());
        }

        // Basso da 0 a 1 s, lead da 2 a 3 s (a 120 BPM una battuta dura 2 s)
        sonora::arrangement::createClipFromFile (*tracks[0], 0_bp, writeSine (dir, "basso", 110.0f));
        sonora::arrangement::createClipFromFile (*tracks[1], 4_bp, writeSine (dir, "lead", 880.0f));
        edit->dispatchPendingUpdatesSynchronously();

        beginTest ("Tratto: dall'inizio alla fine dell'ultima clip, o il loop");
        {
            auto song = ex::timeRangeFor (*edit, ex::Range::song);
            expect (song.has_value());
            expectWithinAbsoluteError (song->getEnd().inSeconds(), 3.0, 0.01);

            edit->getTransport().setLoopRange ({ 2_tp, 3_tp });
            auto loop = ex::timeRangeFor (*edit, ex::Range::loop);
            expect (loop.has_value());
            expectWithinAbsoluteError (loop->getLength().inSeconds(), 1.0, 0.001);
        }

        beginTest ("Mix in WAV a 24 bit");
        {
            ex::Settings s;
            s.bitDepth = 24;
            s.includeTails = false;
            expectEquals (ex::defaultDestination (*edit, s).getFileName(), juce::String ("Brano di prova.wav"));
            s.destination = dir.getChildFile ("Brano di prova.wav");

            juce::Array<juce::File> files;
            auto r = run (*edit, s, files, player);
            expect (r.wasOk(), r.getErrorMessage());
            expectEquals (files.size(), 1);

            auto info = readFile (engine, s.destination);
            expect (info.ok);
            expectEquals (info.bits, 24);
            expectWithinAbsoluteError (info.seconds, 3.0, 0.05);
            expect (info.peakIn (0.1, 0.9) > 0.1f, "basso: " + juce::String (info.peakIn (0.1, 0.9)));
            expect (info.peakIn (1.2, 1.8) < 0.01f, "pausa: " + juce::String (info.peakIn (1.2, 1.8)));
            expect (info.peakIn (2.1, 2.9) > 0.1f, "lead: " + juce::String (info.peakIn (2.1, 2.9)));
        }

        beginTest ("FLAC a 16 bit e normalizzazione");
        {
            ex::Settings s;
            s.format = ex::Format::flac;
            s.bitDepth = 16;
            s.normalise = true;
            s.includeTails = false;
            s.destination = dir.getChildFile ("mix.flac");

            juce::Array<juce::File> files;
            auto r = run (*edit, s, files, player);
            expect (r.wasOk(), r.getErrorMessage());

            auto info = readFile (engine, s.destination);
            expect (info.ok);
            expectEquals (info.bits, 16);
            expectWithinAbsoluteError (info.peak, juce::Decibels::decibelsToGain (-0.3f), 0.03f);
        }

        beginTest ("Solo il loop");
        {
            ex::Settings s;
            s.range = ex::Range::loop;
            s.includeTails = false;
            s.destination = dir.getChildFile ("loop.wav");

            juce::Array<juce::File> files;
            auto r = run (*edit, s, files, player);
            expect (r.wasOk(), r.getErrorMessage());

            auto info = readFile (engine, s.destination);
            expectWithinAbsoluteError (info.seconds, 1.0, 0.02);
            expect (info.peakIn (0.1, 0.9) > 0.1f);   // dentro c'e' il lead
        }

        beginTest ("Stem: un file per traccia, le tracce vuote restano fuori");
        {
            ex::Settings s;
            s.stems = true;
            s.includeTails = false;
            expectEquals (ex::defaultDestination (*edit, s).getFileName(), juce::String ("Brano di prova - stem"));
            s.destination = dir.getChildFile ("Brano di prova - stem");

            juce::Array<juce::File> files;
            auto r = run (*edit, s, files, player);
            expect (r.wasOk(), r.getErrorMessage());
            expectEquals (files.size(), 2);

            auto bass = readFile (engine, s.destination.getChildFile ("01 Basso.wav"));
            auto lead = readFile (engine, s.destination.getChildFile ("02 Lead.wav"));
            expect (bass.ok && lead.ok);
            expect (! s.destination.getChildFile ("03 Vuota.wav").exists());

            // Ogni stem contiene solo la sua traccia, allineata al brano
            expect (bass.peakIn (0.1, 0.9) > 0.1f);
            expect (bass.peakIn (2.1, 2.9) < 0.01f);
            expect (lead.peakIn (0.1, 0.9) < 0.01f);
            expect (lead.peakIn (2.1, 2.9) > 0.1f);
            expectWithinAbsoluteError (lead.seconds, bass.seconds, 0.01);
        }

        beginTest ("MP3 (se c'e' la libreria LAME)");
        {
            if (ex::isAvailable (engine, ex::Format::mp3))
            {
                ex::Settings s;
                s.format = ex::Format::mp3;
                s.mp3Kbps = 192;
                s.includeTails = false;
                s.destination = dir.getChildFile ("mix.mp3");

                juce::Array<juce::File> files;
                auto r = run (*edit, s, files, player);
                expect (r.wasOk(), r.getErrorMessage());
                // 3 secondi a 192 kb/s: circa 72 KB
                expect (s.destination.getSize() > 50000 && s.destination.getSize() < 110000,
                        juce::String (s.destination.getSize()));
            }
            else
            {
                logMessage ("LAME non trovata: test MP3 saltato");
            }
        }

        dir.deleteRecursively();
    }
};

static ExportTests exportTests;

} // namespace
