/*
    Sonora - test della cattura retroattiva.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Arrangement.h"
#include "session/Capture.h"
#include "session/Notes.h"
#include "session/Recording.h"
#include "session/Session.h"
#include "HostedPlayer.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

using sonora::test::HostedPlayer;
namespace cap = sonora::capture;
namespace rec = sonora::recording;

std::vector<std::pair<double, double>> noteTimes (te::MidiClip& c)
{
    std::vector<std::pair<double, double>> result;

    for (auto n : c.getSequence().getNotes())
        result.emplace_back (n->getStartBeat().inBeats(), n->getLengthBeats().inBeats());

    std::sort (result.begin(), result.end());
    return result;
}

struct CaptureTests final : public juce::UnitTest
{
    CaptureTests() : UnitTest ("Cattura retroattiva", "Sonora") {}

    void runTest() override
    {
        te::Engine engine ("SonoraCaptureTests", nullptr, nullptr);
        HostedPlayer player (engine);
        engine.getDeviceManager().dispatchPendingUpdates();

        auto edit = te::createEmptyEdit (engine, juce::File::createTempFile (".tracktionedit"));
        sonora::session::setUpFilePaths (*edit);
        edit->ensureNumberOfAudioTracks (2);
        sonora::session::ensureGrid (*edit);
        edit->getTransport().ensureContextAllocated();
        edit->dispatchPendingUpdatesSynchronously();
        player.pump();

        auto tracks = te::getAudioTracks (*edit);
        auto& t1 = *tracks[0];
        auto& t2 = *tracks[1];
        rec::Recorder recorder (*edit);
        recorder.setSelectedTrack (&t1);

        cap::Capturer capturer (engine);
        capturer.setEdit (edit.get());
        player.afterBlock = [&capturer] { capturer.updateTransportMapping(); };

        const double sr = player.params.sampleRate;
        auto& transport = edit->getTransport();

        // Note a tempi in secondi a partire da adesso (posizione del dispositivo)
        auto playNotes = [&] (std::vector<std::tuple<double, int, double>> notes)
        {
            const auto base = capturer.getDevicePosition();

            for (auto [time, pitch, length] : notes)
            {
                capturer.addMidiAt (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 90), base + (int64_t) (time * sr));
                capturer.addMidiAt (juce::MidiMessage::noteOff (1, pitch), base + (int64_t) ((time + length) * sr));
            }
        };

        beginTest ("Niente da catturare");
        {
            expect (! capturer.hasSomethingFor (t1));
            auto res = capturer.captureArmedTracks (recorder, cap::Destination::session);
            expect (res.failed());
        }

        beginTest ("MIDI a trasporto fermo: un loop nuovo nello slot");
        {
            expect (! transport.isPlaying());
            player.process (0.5);

            // Quattro note ogni mezzo secondo (a 120 BPM: un beat)
            playNotes ({ { 0.0, 60, 0.25 }, { 0.5, 62, 0.25 }, { 1.0, 64, 0.25 }, { 1.5, 65, 0.25 } });
            player.process (2.5);

            expect (capturer.hasSomethingFor (t1));
            juce::Array<te::Clip*> clips;
            auto res = capturer.captureArmedTracks (recorder, cap::Destination::session, &clips);
            expect (res.wasOk(), res.getErrorMessage());
            expectEquals (clips.size(), 1);

            auto clip = dynamic_cast<te::MidiClip*> (clips.getFirst());
            expect (clip != nullptr && clip->getClipSlot() == sonora::session::getSlot (t1, 0));

            if (clip != nullptr)
            {
                expectWithinAbsoluteError (clip->getLengthInBeats().inBeats(), 4.0, 0.001);
                expect (clip->isLooping());
                auto notes = noteTimes (*clip);
                expectEquals ((int) notes.size(), 4);

                for (size_t i = 0; i < notes.size(); ++i)
                {
                    expectWithinAbsoluteError (notes[i].first, (double) i, 0.02);
                    expectWithinAbsoluteError (notes[i].second, 0.5, 0.02);
                }
            }

            // Gia' catturate: non ci sono altre note
            expect (! capturer.hasSomethingFor (t1));
            expect (capturer.captureArmedTracks (recorder, cap::Destination::session).failed());
        }

        beginTest ("Solo l'ultima idea, dopo una pausa lunga");
        {
            playNotes ({ { 0.0, 48, 0.2 } });
            player.process (8.0);
            playNotes ({ { 0.0, 72, 0.4 }, { 1.0, 74, 0.4 }, { 3.0, 76, 0.4 } });
            player.process (4.0);

            te::Clip* clip = nullptr;
            auto res = capturer.captureTrack (t1, cap::Destination::session, &recorder, &clip);
            expect (res.wasOk(), res.getErrorMessage());

            if (auto mc = dynamic_cast<te::MidiClip*> (clip))
            {
                auto notes = noteTimes (*mc);
                expectEquals ((int) notes.size(), 3);
                // 3,4 secondi = 6,8 beat: servono due battute
                expectWithinAbsoluteError (mc->getLengthInBeats().inBeats(), 8.0, 0.001);
                expect (mc->getClipSlot() == sonora::session::getSlot (t1, 1));
            }
        }

        beginTest ("MIDI a tempo: allineato alle battute nell'arrangement");
        {
            transport.setPosition (0_tp);
            transport.play (false);
            player.process (1.0);
            expect (transport.isPlaying());

            // Dove saremo, in beat del progetto, per ogni campione del dispositivo
            const auto& ts = edit->tempoSequence;
            const double latency = engine.getDeviceManager().getOutputLatencySeconds();
            const auto offset = capturer.getTransportOffset();
            expect (offset.has_value());
            auto devFor = [&] (double beat)
            {
                return (int64_t) std::llround ((ts.toTime (te::BeatPosition::fromBeats (beat)).inSeconds() + latency) * sr) - offset.value_or (0);
            };

            // Una frase che comincia sulla seconda battuta (beat 4), con la prima nota un po' in anticipo
            for (auto [beat, pitch] : std::vector<std::pair<double, int>> { { 3.95, 60 }, { 5.0, 62 }, { 6.0, 64 }, { 7.5, 67 } })
            {
                capturer.addMidiAt (juce::MidiMessage::noteOn (1, pitch, (juce::uint8) 100), devFor (beat));
                capturer.addMidiAt (juce::MidiMessage::noteOff (1, pitch), devFor (beat + 0.25));
            }

            player.process (3.5);

            te::Clip* clip = nullptr;
            auto res = capturer.captureTrack (t1, cap::Destination::arrangement, &recorder, &clip);
            expect (res.wasOk(), res.getErrorMessage());
            auto mc = dynamic_cast<te::MidiClip*> (clip);
            expect (mc != nullptr && mc->getClipSlot() == nullptr);

            if (mc != nullptr)
            {
                expectWithinAbsoluteError (mc->getStartBeat().inBeats(), 4.0, 0.001);
                expectWithinAbsoluteError (mc->getLengthInBeats().inBeats(), 4.0, 0.001);
                auto notes = noteTimes (*mc);
                expectEquals ((int) notes.size(), 4);

                if (notes.size() == 4)
                {
                    // L'anticipo resta sulla battuta (non si sposta la clip alla battuta prima)
                    expectWithinAbsoluteError (notes[0].first, 0.0, 0.02);
                    expectWithinAbsoluteError (notes[1].first, 1.0, 0.02);
                    expectWithinAbsoluteError (notes[2].first, 2.0, 0.02);
                    expectWithinAbsoluteError (notes[3].first, 3.5, 0.02);
                }
            }
        }

        beginTest ("MIDI a tempo nella session: la clip suona subito, in fase");
        {
            const auto& ts = edit->tempoSequence;
            auto context = transport.getCurrentPlaybackContext();
            const double beatNow = ts.toBeats (context->getPosition()).inBeats();
            const double latency = engine.getDeviceManager().getOutputLatencySeconds();
            const double barStart = std::ceil (beatNow / 4.0) * 4.0;
            const auto offset = capturer.getTransportOffset();
            auto devFor = [&] (double beat)
            {
                return (int64_t) std::llround ((ts.toTime (te::BeatPosition::fromBeats (beat)).inSeconds() + latency) * sr) - offset.value_or (0);
            };

            for (int i = 0; i < 4; ++i)
            {
                capturer.addMidiAt (juce::MidiMessage::noteOn (1, 60 + i, (juce::uint8) 100), devFor (barStart + i));
                capturer.addMidiAt (juce::MidiMessage::noteOff (1, 60 + i), devFor (barStart + i + 0.5));
            }

            player.process ((barStart + 4.5 - beatNow) * 0.5);

            te::Clip* clip = nullptr;
            auto res = capturer.captureTrack (t1, cap::Destination::session, &recorder, &clip);
            expect (res.wasOk(), res.getErrorMessage());
            expect (clip != nullptr && clip->getClipSlot() != nullptr);

            player.process (0.5);

            if (clip != nullptr)
            {
                expect (sonora::session::getSlotState (*clip->getClipSlot()) == sonora::session::SlotState::playing);

                // Dove dovrebbe essere: (beat attuale - inizio della frase) modulo la lunghezza
                const double beat = ts.toBeats (context->getPosition()).inBeats();
                const double expected = std::fmod (beat - barStart, 4.0) / 4.0;

                if (auto progress = sonora::session::getPlayProgress (*clip))
                {
                    double diff = std::abs (*progress - expected);
                    diff = std::min (diff, 1.0 - diff);
                    expect (diff < 0.03, "fuori fase: " + juce::String (*progress) + " invece di " + juce::String (expected));
                }
                else
                {
                    expect (false, "la clip catturata non suona");
                }
            }

            transport.stop (false, false);
            player.process (0.2);
        }

        beginTest ("Audio: la frase suonata diventa una clip audio");
        {
            juce::String audioInput;

            for (auto& c : rec::getInputChoices (*edit))
                if (c.id.startsWith ("audio:"))
                    audioInput = c.id;

            expect (audioInput.isNotEmpty(), "nessun ingresso audio");

            if (audioInput.isNotEmpty())
            {
                rec::setInput (t2, audioInput);
                recorder.setArmed (t2, true);

                player.process (3.0);                 // silenzio
                expect (! capturer.hasSomethingFor (t2));
                player.process (1.0, {}, true);       // un secondo di suono
                player.process (0.5);
                expect (capturer.hasSomethingFor (t2));

                te::Clip* clip = nullptr;
                auto res = capturer.captureTrack (t2, cap::Destination::session, &recorder, &clip);
                expect (res.wasOk(), res.getErrorMessage());

                auto wave = dynamic_cast<te::WaveAudioClip*> (clip);
                expect (wave != nullptr);

                if (wave != nullptr)
                {
                    const auto f = wave->getSourceFileReference().getFile();
                    expect (f.existsAsFile());
                    // Un secondo a 120 BPM = due beat: arrotondato a una battuta
                    expectWithinAbsoluteError (wave->getLengthInBeats().inBeats(), 4.0, 0.01);

                    te::AudioFile af (engine, f);
                    expectWithinAbsoluteError (af.getLength(), 2.0, 0.01);

                    // Il suono c'e', all'inizio (dopo un attimo di margine) e non alla fine
                    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (f)))
                    {
                        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
                        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);
                        expect (buffer.getMagnitude (0, (int) (0.1 * sr), (int) (0.5 * sr)) > 0.3f);
                        expect (buffer.getMagnitude (0, (int) (1.2 * sr), (int) (0.7 * sr)) < 0.01f);
                    }
                }

                expect (! capturer.hasSomethingFor (t2));
            }
        }

        player.afterBlock = nullptr;
    }
};

static CaptureTests captureTests;

} // namespace
