/*
    Sonora - test della logica della session view.
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Session.h"

namespace te = tracktion;
using sonora::session::SlotState;

namespace
{

struct SessionTests final : public juce::UnitTest
{
    SessionTests() : UnitTest ("Session view", "Sonora") {}

    void runTest() override
    {
        te::Engine engine ("SonoraSessionTests", nullptr, nullptr);
        namespace session = sonora::session;

        auto file = juce::File::createTempFile (".tracktionedit");
        auto edit = te::createEmptyEdit (engine, file);
        edit->ensureNumberOfAudioTracks (2);

        beginTest ("Griglia: scene e slot allineati su tutte le tracce");
        {
            session::ensureGrid (*edit);
            expectEquals (edit->getSceneList().getNumScenes(), session::defaultNumScenes);

            for (auto t : te::getAudioTracks (*edit))
            {
                expectEquals (t->getClipSlotList().getClipSlots().size(), session::defaultNumScenes);
                expect (! t->getColour().isTransparent());
            }

            auto added = session::addTrack (*edit);
            expect (added != nullptr);
            expectEquals (te::getAudioTracks (*edit).size(), 3);
            expectEquals (added->getClipSlotList().getClipSlots().size(), session::defaultNumScenes);

            session::addScene (*edit);
            expectEquals (edit->getSceneList().getNumScenes(), session::defaultNumScenes + 1);

            for (auto t : te::getAudioTracks (*edit))
                expectEquals (t->getClipSlotList().getClipSlots().size(), session::defaultNumScenes + 1);
        }

        auto tracks = te::getAudioTracks (*edit);
        auto& t1 = *tracks[0];
        auto& t2 = *tracks[1];

        beginTest ("Clip MIDI: creazione, lunghezza in battute e loop");
        {
            auto slot = session::getSlot (t1, 0);
            expect (session::getSlotState (*slot) == SlotState::empty);

            auto clip = session::createMidiClip (*slot, 2);
            expect (clip != nullptr);
            expect (slot->getClip() == clip);
            expect (clip->isLooping());
            expectWithinAbsoluteError (clip->getLengthInBeats().inBeats(), 8.0, 1.0e-6);
            expect (session::getSlotState (*slot) == SlotState::stopped);
        }

        beginTest ("Undo e redo della creazione di una clip");
        {
            auto slot = session::getSlot (t2, 1);
            session::createMidiClip (*slot, 1);
            expect (slot->getClip() != nullptr);

            edit->getUndoManager().beginNewTransaction();
            edit->undo();
            expect (slot->getClip() == nullptr);

            edit->redo();
            expect (slot->getClip() != nullptr);
        }

        beginTest ("Clip da file MIDI");
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0.0);
            seq.addEvent (juce::MidiMessage::noteOff (1, 60), 480.0);
            seq.updateMatchedPairs();

            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (480);
            mf.addTrack (seq);

            juce::TemporaryFile tmp (".mid");
            {
                juce::FileOutputStream out (tmp.getFile());
                mf.writeTo (out);
            }

            auto slot = session::getSlot (t2, 2);
            auto clip = session::createClipFromFile (*slot, tmp.getFile());
            expect (clip != nullptr);

            if (auto mc = dynamic_cast<te::MidiClip*> (clip))
                expectEquals (mc->getSequence().getNumNotes(), 1);

            expect (session::createClipFromFile (*session::getSlot (t2, 3), juce::File::createTempFile (".txt")) == nullptr);
        }

        beginTest ("Lancio: la clip parte e le altre della traccia si fermano");
        {
            auto first = session::getSlot (t1, 0)->getClip();
            auto secondSlot = session::getSlot (t1, 1);
            auto second = session::createMidiClip (*secondSlot, 1);

            session::launchClip (*first);
            const auto s1 = session::getSlotState (*session::getSlot (t1, 0));
            expect (s1 == SlotState::playQueued || s1 == SlotState::playing);

            session::launchClip (*second);
            const auto s2 = session::getSlotState (*secondSlot);
            expect (s2 == SlotState::playQueued || s2 == SlotState::playing);

            // La prima clip non deve piu' partire.
            const auto s1After = session::getSlotState (*session::getSlot (t1, 0));
            expect (s1After == SlotState::stopQueued || s1After == SlotState::stopped);

            // Una clip mai lanciata non deve risultare in attesa di stop.
            expect (session::getSlotState (*session::getSlot (t2, 1)) == SlotState::stopped);
        }

        beginTest ("Scene: lancia la riga, gli slot vuoti non fermano la traccia");
        {
            session::stopAllClips (*edit);
            auto t2Clip = session::getSlot (t2, 1)->getClip();
            session::launchClip (*t2Clip);

            // Scena 0: t1 ha una clip, t2 no.
            session::launchScene (*edit, 0);

            const auto t1State = session::getSlotState (*session::getSlot (t1, 0));
            expect (t1State == SlotState::playQueued || t1State == SlotState::playing);

            const auto t2State = session::getSlotState (*session::getSlot (t2, 1));
            expect (t2State == SlotState::playQueued || t2State == SlotState::playing);

            session::stopTrack (t2);
            const auto t2Stopped = session::getSlotState (*session::getSlot (t2, 1));
            expect (t2Stopped == SlotState::stopQueued || t2Stopped == SlotState::stopped);
        }

        edit->getTransport().stop (false, true);

        beginTest ("Salvataggio e riapertura: clip e scene restano negli slot");
        {
            session::getSlot (t1, 0)->getClip()->setName ("Accordi");
            te::EditFileOperations (*edit).save (false, true, false);
            const int numScenes = edit->getSceneList().getNumScenes();
            edit = nullptr;

            auto reloaded = sonora::session::loadEdit (engine, file);
            expect (reloaded != nullptr);

            if (reloaded != nullptr)
            {
                expectEquals (reloaded->getSceneList().getNumScenes(), numScenes);
                auto rt = te::getAudioTracks (*reloaded);
                expectEquals (rt.size(), 3);

                auto clip = session::getSlot (*rt[0], 0)->getClip();
                expect (clip != nullptr);

                if (clip != nullptr)
                    expectEquals (clip->getName(), juce::String ("Accordi"));

                expect (session::getSlot (*rt[1], 2)->getClip() != nullptr);
            }
        }

        beginTest ("Elimina traccia e undo");
        {
            auto e2 = te::createEmptyEdit (engine, juce::File::createTempFile (".tracktionedit"));
            e2->ensureNumberOfAudioTracks (2);
            session::ensureGrid (*e2);
            e2->getUndoManager().clearUndoHistory();

            session::deleteTrack (*te::getAudioTracks (*e2)[1]);
            expectEquals (te::getAudioTracks (*e2).size(), 1);

            e2->undo();
            expectEquals (te::getAudioTracks (*e2).size(), 2);
        }

        beginTest ("Ricerca per corrispondenza dei nomi di quantizzazione");
        {
            expectEquals (session::getQuantisationNames().size(), (int) te::LaunchQType::sixtyFourthD + 1);
        }

        file.deleteFile();
    }
};

SessionTests sessionTests;

} // namespace
