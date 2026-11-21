/*
    Sonora - test dell'editing delle note (piano roll).
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Notes.h"
#include "session/Session.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

struct NotesTests final : public juce::UnitTest
{
    NotesTests() : UnitTest ("Piano roll", "Sonora") {}

    void runTest() override
    {
        namespace notes = sonora::notes;
        te::Engine engine ("SonoraNotesTests", nullptr, nullptr);
        auto edit = te::createEmptyEdit (engine, juce::File::createTempFile (".tracktionedit"));
        edit->ensureNumberOfAudioTracks (1);
        sonora::session::ensureGrid (*edit);

        auto& track = *te::getAudioTracks (*edit)[0];
        auto clip = sonora::session::createMidiClip (*sonora::session::getSlot (track, 0), 1);
        auto& um = edit->getUndoManager();
        const auto sixteenth = te::BeatDuration::fromBeats (0.25);

        beginTest ("Griglia");
        {
            expectWithinAbsoluteError (notes::snapDown (te::BeatPosition::fromBeats (1.3), sixteenth).inBeats(), 1.25, 1.0e-9);
            expectWithinAbsoluteError (notes::snapNearest (te::BeatPosition::fromBeats (1.4), sixteenth).inBeats(), 1.5, 1.0e-9);
            expectWithinAbsoluteError (notes::snapDown (te::BeatPosition::fromBeats (0.9999999), 1_bd).inBeats(), 1.0, 1.0e-9);
            expectEquals (notes::noteName (60), juce::String ("C3"));
        }

        beginTest ("Aggiungere note: dentro la clip e limitate alla sua fine");
        {
            um.beginNewTransaction();
            auto n = notes::addNote (*clip, 60, 0_bp, 1_bd, 90);
            expect (n != nullptr);
            expectEquals (n->getVelocity(), 90);

            auto tail = notes::addNote (*clip, 64, 3.5_bp, 2_bd);
            expect (tail != nullptr);
            expectWithinAbsoluteError (tail->getLengthBeats().inBeats(), 0.5, 1.0e-9);

            expect (notes::addNote (*clip, 60, 4_bp, 1_bd) == nullptr);
            expect (notes::addNote (*clip, 128, 0_bp, 1_bd) == nullptr);
            expectEquals (clip->getSequence().getNumNotes(), 2);

            expect (notes::findNote (*clip, 60, 0.5_bp) == n);
            expect (notes::findNote (*clip, 60, 1_bp) == nullptr);
            expect (notes::findNote (*clip, 61, 0.5_bp) == nullptr);
        }

        beginTest ("Spostare e allungare restando dentro i limiti");
        {
            auto n = notes::findNote (*clip, 60, 0_bp);
            um.beginNewTransaction();
            notes::moveNote (*clip, *n, 2_bp, 67);
            expectWithinAbsoluteError (n->getStartBeat().inBeats(), 2.0, 1.0e-9);
            expectEquals (n->getNoteNumber(), 67);

            notes::moveNote (*clip, *n, 10_bp, 200);
            expectWithinAbsoluteError (n->getStartBeat().inBeats(), 3.0, 1.0e-9);
            expectEquals (n->getNoteNumber(), 127);

            notes::moveNote (*clip, *n, 1_bp, 67);
            notes::resizeNote (*clip, *n, 10_bd, sixteenth);
            expectWithinAbsoluteError (n->getLengthBeats().inBeats(), 3.0, 1.0e-9);

            notes::resizeNote (*clip, *n, 0_bd, sixteenth);
            expectWithinAbsoluteError (n->getLengthBeats().inBeats(), 0.25, 1.0e-9);

            notes::setVelocity (*clip, *n, 300);
            expectEquals (n->getVelocity(), 127);
        }

        beginTest ("Quantizzazione");
        {
            um.beginNewTransaction();
            auto n = notes::addNote (*clip, 50, te::BeatPosition::fromBeats (0.6), sixteenth);
            notes::quantise (*clip, 0.5_bd);
            expectWithinAbsoluteError (n->getStartBeat().inBeats(), 0.5, 1.0e-9);
        }

        beginTest ("Eliminare e annullare");
        {
            const int before = clip->getSequence().getNumNotes();
            um.beginNewTransaction();
            notes::deleteNote (*clip, *clip->getSequence().getNotes().getFirst());
            expectEquals (clip->getSequence().getNumNotes(), before - 1);

            um.beginNewTransaction();
            edit->undo();
            expectEquals (clip->getSequence().getNumNotes(), before);
        }

        beginTest ("Lunghezza della clip in battute");
        {
            um.beginNewTransaction();
            notes::setClipLengthInBars (*clip, 4);
            expectWithinAbsoluteError (notes::getClipLength (*clip).inBeats(), 16.0, 1.0e-6);
            expect (notes::addNote (*clip, 60, 12_bp, 1_bd) != nullptr);

            notes::setClipLengthInBars (*clip, 2);
            expectWithinAbsoluteError (notes::getClipLength (*clip).inBeats(), 8.0, 1.0e-6);
        }
    }
};

NotesTests notesTests;

} // namespace
