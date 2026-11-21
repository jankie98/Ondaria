/*
    Sonora - test della logica dell'arrangement.
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Arrangement.h"
#include "session/Session.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

struct ArrangementTests final : public juce::UnitTest
{
    ArrangementTests() : UnitTest ("Arrangement", "Sonora") {}

    void runTest() override
    {
        namespace arr = sonora::arrangement;
        te::Engine engine ("SonoraArrangementTests", nullptr, nullptr);
        auto edit = te::createEmptyEdit (engine, juce::File::createTempFile (".tracktionedit"));
        edit->ensureNumberOfAudioTracks (2);
        sonora::session::ensureGrid (*edit);
        auto& um = edit->getUndoManager();
        auto tracks = te::getAudioTracks (*edit);
        auto& t1 = *tracks[0];
        auto& t2 = *tracks[1];

        beginTest ("Nuova clip MIDI sulla linea del tempo, separata dagli slot");
        {
            um.beginNewTransaction();
            auto c = arr::createMidiClip (t1, 8_bp, 2);
            expect (c != nullptr);
            expectWithinAbsoluteError (c->getStartBeat().inBeats(), 8.0, 1.0e-6);
            expectWithinAbsoluteError ((c->getEndBeat() - c->getStartBeat()).inBeats(), 8.0, 1.0e-6);
            expectEquals (arr::getClips (t1).size(), 1);
            expect (sonora::session::getSlot (t1, 0)->getClip() == nullptr);

            expect (arr::createMidiClip (t1, te::BeatPosition::fromBeats (-3.0), 1)->getStartBeat() == 0_bp);
            expectEquals (arr::getClips (t1).size(), 2);
        }

        beginTest ("Spostare tra le tracce e ridimensionare");
        {
            auto c = arr::getClips (t1).getFirst();

            if (c->getStartBeat() != 8_bp)
                c = arr::getClips (t1)[1];

            um.beginNewTransaction();
            arr::moveClip (*c, 16_bp, &t2);
            expectEquals (arr::getClips (t2).size(), 1);
            expectEquals (arr::getClips (t1).size(), 1);

            auto moved = arr::getClips (t2).getFirst();
            expectWithinAbsoluteError (moved->getStartBeat().inBeats(), 16.0, 1.0e-6);

            arr::resizeClip (*moved, 2_bd, 1_bd);
            expectWithinAbsoluteError ((moved->getEndBeat() - moved->getStartBeat()).inBeats(), 2.0, 1.0e-6);

            arr::resizeClip (*moved, 0_bd, 1_bd);
            expectWithinAbsoluteError ((moved->getEndBeat() - moved->getStartBeat()).inBeats(), 1.0, 1.0e-6);

            um.beginNewTransaction();
            edit->undo();
            expectEquals (arr::getClips (t1).size(), 2);
            expectEquals (arr::getClips (t2).size(), 0);
        }

        beginTest ("Duplicare ed eliminare");
        {
            auto c = arr::getClips (t1).getLast();
            um.beginNewTransaction();
            auto copy = arr::duplicateClip (*c);
            expect (copy != nullptr);
            expect (copy->itemID != c->itemID);
            expectWithinAbsoluteError (copy->getStartBeat().inBeats(), c->getEndBeat().inBeats(), 1.0e-6);
            expectEquals (arr::getClips (t1).size(), 3);

            um.beginNewTransaction();
            arr::deleteClip (*copy);
            expectEquals (arr::getClips (t1).size(), 2);
        }

        beginTest ("Fine del contenuto");
        {
            expectWithinAbsoluteError (arr::getEndOfContent (*edit, 1).inBeats(), 16.0, 1.0e-6);
            expectWithinAbsoluteError (arr::getEndOfContent (*edit, 16).inBeats(), 64.0, 1.0e-6);
        }

        beginTest ("Torna all'arrangement");
        {
            t1.playSlotClips = true;
            expect (arr::isAnyTrackPlayingSession (*edit));
            arr::backToArrangement (*edit);
            expect (! arr::isAnyTrackPlayingSession (*edit));
        }

        beginTest ("Import di un file MIDI nella linea del tempo");
        {
            juce::MidiMessageSequence seq;
            seq.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0.0);
            seq.addEvent (juce::MidiMessage::noteOff (1, 64), 960.0);
            seq.updateMatchedPairs();
            juce::MidiFile mf;
            mf.setTicksPerQuarterNote (480);
            mf.addTrack (seq);
            juce::TemporaryFile tmp (".mid");
            {
                juce::FileOutputStream out (tmp.getFile());
                mf.writeTo (out);
            }

            um.beginNewTransaction();
            auto c = arr::createClipFromFile (t2, 4_bp, tmp.getFile());
            expect (c != nullptr);

            if (c != nullptr)
                expectWithinAbsoluteError (c->getStartBeat().inBeats(), 4.0, 1.0e-6);
        }

        beginTest ("Griglia adattiva allo zoom");
        {
            // Zoom normale (24 px per beat): un beat; ingrandendo si infittisce
            expectEquals (arr::adaptiveGridBeats (24.0, 4.0), 1.0);
            expectEquals (arr::adaptiveGridBeats (60.0, 4.0), 0.25);
            expectEquals (arr::adaptiveGridBeats (400.0, 4.0), 0.125);
            // Allontanando: battute intere, poi gruppi di battute
            expectEquals (arr::adaptiveGridBeats (6.0, 4.0), 4.0);
            expectEquals (arr::adaptiveGridBeats (2.0, 4.0), 8.0);
            expectEquals (arr::adaptiveGridBeats (2.0, 3.0), 12.0);

            expectEquals (arr::gridName (0.25, 4.0), juce::String ("1/16"));
            expectEquals (arr::gridName (1.0, 4.0), juce::String ("1/4"));
            expectEquals (arr::gridName (4.0, 4.0), juce::String ("1 battuta"));
            expectEquals (arr::gridName (16.0, 4.0), juce::String ("4 battute"));
        }
    }
};

ArrangementTests arrangementTests;

} // namespace
