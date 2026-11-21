/*
    Sonora - test della registrazione.

    Il motore gira su un dispositivo audio "ospitato": i blocchi li elaboriamo
    noi, con il MIDI e l'audio in ingresso che vogliamo, quindi la
    registrazione avviene davvero anche senza scheda audio.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Arrangement.h"
#include "session/Recording.h"
#include "session/Session.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

/** Fa girare il motore a blocchi, con note MIDI in ingresso a tempi dati. */
class HostedPlayer
{
public:
    HostedPlayer (te::Engine& e)
        : engine (e), hosted (e.getDeviceManager().getHostedAudioDeviceInterface())
    {
        params.sampleRate = 44100.0;
        params.blockSize = 512;
        params.inputChannels = 2;
        params.outputChannels = 2;
        params.useMidiDevices = false;

        engine.getDeviceManager().setCpuLimitBeforeMuting (1000.0);
        hosted.initialise (params);
        hosted.prepareToPlay (params.sampleRate, params.blockSize);
        engine.getDeviceManager().dispatchPendingUpdates();
    }

    ~HostedPlayer()
    {
        engine.getDeviceManager().deviceManager.closeAudioDevice();
        engine.getDeviceManager().removeHostedAudioDeviceInterface();
    }

    /** Elabora seconds secondi. Le note (tempo in secondi da adesso, nota, on/off)
        entrano dall'ingresso MIDI; in ingresso audio c'e' un seno se sine e' true. */
    void process (double seconds, std::vector<std::tuple<double, int, bool>> notes = {}, bool sine = false)
    {
        const auto total = (int) std::round (seconds * params.sampleRate);

        for (int done = 0; done < total;)
        {
            const int num = std::min (params.blockSize, total - done);
            juce::AudioBuffer<float> buffer (2, num);
            buffer.clear();

            if (sine)
                for (int i = 0; i < num; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 220.0f
                                                                   * (float) (samplePos + i) / (float) params.sampleRate));

            juce::MidiBuffer midi;

            for (auto [time, note, on] : notes)
            {
                const auto pos = (int) std::round (time * params.sampleRate);

                if (pos >= done && pos < done + num)
                    midi.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100)
                                      : juce::MidiMessage::noteOff (1, note),
                                   pos - done);
            }

            hosted.processBlock (buffer, midi);
            done += num;
            samplePos += num;

            // I timer del motore (fine delle registrazioni, aggiornamenti) girano sul thread dei messaggi.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
    }

    void pump (int ms = 50)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
    }

    te::Engine& engine;
    te::HostedAudioDeviceInterface& hosted;
    te::HostedAudioDeviceInterface::Parameters params;
    int64_t samplePos = 0;
};

int countNotes (te::Clip* c)
{
    if (auto mc = dynamic_cast<te::MidiClip*> (c))
        return mc->getSequence().getNumNotes();

    return 0;
}

te::InputDeviceInstance* findMidiInput (te::Edit& e)
{
    for (auto inst : e.getAllInputDevices())
        if (inst->getInputDevice().getDeviceType() == te::InputDevice::physicalMidiDevice
             || inst->getInputDevice().getDeviceType() == te::InputDevice::virtualMidiDevice)
            return inst;

    return nullptr;
}

bool isTarget (te::InputDeviceInstance& inst, te::AudioTrack& t, bool armed)
{
    for (auto dest : inst.destinations)
        if (dest->targetID == t.itemID)
            return dest->recordEnabled.get() == armed;

    return false;
}

struct RecordingTests final : public juce::UnitTest
{
    RecordingTests() : UnitTest ("Registrazione", "Sonora") {}

    void runTest() override
    {
        namespace rec = sonora::recording;
        te::Engine engine ("SonoraRecordingTests", nullptr, nullptr);
        HostedPlayer player (engine);

        for (auto& m : engine.getDeviceManager().getMidiInDevices())
        {
            m->setEnabled (true);
            m->setMonitorMode (te::InputDevice::MonitorMode::on);
        }

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
        auto midiIn = findMidiInput (*edit);

        beginTest ("Ingressi e armamento");
        {
            expect (midiIn != nullptr);

            if (midiIn == nullptr)
                return;

            expect (rec::getInput (t1) == rec::midiInputID);
            expect (! recorder.isArmed (t1));

            // La traccia selezionata con ingresso MIDI e' armata in automatico
            recorder.setSelectedTrack (&t1);
            expect (recorder.isArmed (t1) && recorder.isAutoArmed (t1));
            expect (isTarget (*midiIn, t1, true));
            expect (! isTarget (*midiIn, t2, false) && ! isTarget (*midiIn, t2, true));

            // Armata con il pulsante riceve il MIDI anche se non e' selezionata
            recorder.setArmed (t2, true);
            expect (rec::isExplicitlyArmed (t2));
            expect (isTarget (*midiIn, t2, true));

            // Senza ingresso non e' armata
            rec::setInput (t2, {});
            recorder.syncInputs();
            expect (! recorder.isArmed (t2));
            expect (! isTarget (*midiIn, t2, true) && ! isTarget (*midiIn, t2, false));

            rec::setInput (t2, rec::midiInputID);
            recorder.setArmed (t2, false);
            expect (! isTarget (*midiIn, t2, false) && ! isTarget (*midiIn, t2, true));

            expect (rec::getInputChoices (*edit).size() >= 2);
            expect (rec::getInputName (t1) == "MIDI");
        }

        beginTest ("Senza tracce armate l'arrangement non registra");
        {
            recorder.setSelectedTrack (nullptr);
            expect (recorder.toggleArrangementRecording().failed());
            expect (! edit->getTransport().isRecording());
            recorder.setSelectedTrack (&t1);
            edit->dispatchPendingUpdatesSynchronously();
            player.pump();
        }

        beginTest ("Registrazione MIDI nell'arrangement");
        {
            auto& transport = edit->getTransport();
            transport.setPosition (0_tp);

            expect (recorder.toggleArrangementRecording().wasOk());
            expect (transport.isRecording());

            player.process (1.0, { { 0.2, 60, true }, { 0.4, 60, false }, { 0.5, 64, true }, { 0.7, 64, false } });
            expect (recorder.isRecordingInto (t1));

            expect (recorder.toggleArrangementRecording().wasOk());
            player.pump();
            expect (! transport.isRecording());
            transport.stop (false, false);
            player.process (0.2);
            player.pump();
            expect (! transport.isPlaying(), "Il trasporto deve restare fermo dopo una registrazione");

            auto clips = sonora::arrangement::getClips (t1);
            expectEquals (clips.size(), 1);
            expectEquals (countNotes (clips.getFirst()), 2);
            expect (sonora::session::getSlot (t1, 0)->getClip() == nullptr);
        }

        beginTest ("Registrazione MIDI in uno slot, a tempo");
        {
            auto& transport = edit->getTransport();
            transport.setPosition (0_tp);
            auto slot = sonora::session::getSlot (t1, 1);
            expect (recorder.canRecordInto (*slot));

            // 120 bpm: una battuta = 2 secondi, quantizzazione del lancio di una battuta
            expect (recorder.startSlotRecording (*slot).wasOk());
            expect (transport.isPlaying());
            expect (recorder.getSlotRecordState (*slot) == rec::Recorder::SlotRecordState::recording);

            player.process (1.0, { { 0.1, 67, true }, { 0.3, 67, false }, { 0.6, 69, true }, { 0.8, 69, false } });

            // Si ferma alla fine della battuta e la clip parte in loop da li'
            recorder.stopSlotRecording (*slot);
            expect (recorder.getSlotRecordState (*slot) == rec::Recorder::SlotRecordState::stopping);
            player.process (1.5);
            player.pump (100);
            player.process (0.2);

            auto clip = slot->getClip();
            expect (clip != nullptr);
            expectEquals (countNotes (clip), 2);
            expect (recorder.getSlotRecordState (*slot) == rec::Recorder::SlotRecordState::none);

            if (clip != nullptr)
            {
                expectWithinAbsoluteError (clip->getLengthInBeats().inBeats(), 4.0, 0.1);
                expect (clip->isLooping());
                expect (sonora::session::getSlotState (*slot) == sonora::session::SlotState::playing);
            }

            // Nessuna clip nuova nell'arrangement, e lo slot non resta come destinazione
            expectEquals (sonora::arrangement::getClips (t1).size(), 1);
            player.process (0.1);

            for (auto target : midiIn->getTargets())
                expect (target != slot->itemID);

            recorder.stopAllSlotRecordings();
            transport.stop (false, false);
            sonora::session::stopAllClips (*edit);
            player.pump();
        }

        beginTest ("Stop durante la registrazione dell'arrangement");
        {
            auto& transport = edit->getTransport();
            transport.setPosition (4_tp);
            expect (recorder.toggleArrangementRecording().wasOk());
            player.process (0.5, { { 0.1, 62, true }, { 0.3, 62, false } });

            // Lo stop chiude la registrazione e ferma tutto
            transport.stop (false, false);
            player.process (0.3);
            player.pump (100);
            player.process (0.1);
            expect (! transport.isRecording());
            expect (! transport.isPlaying(), "Il trasporto deve fermarsi");
            expectEquals (sonora::arrangement::getClips (t1).size(), 2);
        }

        beginTest ("Fermare il trasporto chiude la registrazione dello slot");
        {
            auto& transport = edit->getTransport();
            transport.setPosition (0_tp);
            auto slot = sonora::session::getSlot (t1, 2);

            expect (recorder.startSlotRecording (*slot).wasOk());
            player.process (0.8, { { 0.1, 72, true }, { 0.5, 72, false } });

            recorder.stopAllSlotRecordings();
            transport.stop (false, false);
            player.pump();

            expect (slot->getClip() != nullptr);
            expectEquals (countNotes (slot->getClip()), 1);
            expect (recorder.getSlotRecordState (*slot) == rec::Recorder::SlotRecordState::none);
        }

        beginTest ("Registrazione audio in uno slot");
        {
            auto choices = rec::getInputChoices (*edit);
            juce::String audioInput;

            for (auto& c : choices)
                if (c.id.startsWith ("audio:"))
                    audioInput = c.id;

            expect (audioInput.isNotEmpty());

            if (audioInput.isNotEmpty())
            {
                rec::setInput (t2, audioInput);
                recorder.setArmed (t2, true);
                edit->dispatchPendingUpdatesSynchronously();
                player.pump();

                auto& transport = edit->getTransport();
                transport.setPosition (0_tp);
                auto slot = sonora::session::getSlot (t2, 0);
                expect (recorder.startSlotRecording (*slot).wasOk());

                player.process (1.0, {}, true);
                recorder.stopAllSlotRecordings();
                transport.stop (false, false);
                player.pump (200);

                auto wave = dynamic_cast<te::WaveAudioClip*> (slot->getClip());
                expect (wave != nullptr);

                if (wave != nullptr)
                {
                    const auto f = wave->getSourceFileReference().getFile();
                    expect (f.existsAsFile(), "File registrato mancante: " + f.getFullPathName() + " src=" + wave->getSourceFileReference().source);
                    expectWithinAbsoluteError (wave->getPosition().getLength().inSeconds(), 1.0, 0.1);
                }

                recorder.setArmed (t2, false);
            }
        }

        edit->getTransport().stop (false, true);
        player.pump();
    }
};

RecordingTests recordingTests;
} // namespace
