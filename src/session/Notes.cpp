/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Notes.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace sonora::notes
{

namespace
{
    juce::UndoManager* um (te::MidiClip& c)     { return &c.edit.getUndoManager(); }
}

te::BeatPosition snapDown (te::BeatPosition p, te::BeatDuration grid)
{
    if (grid <= 0_bd)
        return p;

    return te::BeatPosition::fromBeats (std::floor (p.inBeats() / grid.inBeats() + 1.0e-6) * grid.inBeats());
}

te::BeatPosition snapNearest (te::BeatPosition p, te::BeatDuration grid)
{
    if (grid <= 0_bd)
        return p;

    return te::BeatPosition::fromBeats (std::round (p.inBeats() / grid.inBeats()) * grid.inBeats());
}

te::BeatDuration getClipLength (te::MidiClip& c)
{
    if (c.isLooping())
        return c.getLoopLengthBeats();

    return c.getLengthInBeats();
}

void setClipLengthInBars (te::MidiClip& c, int bars)
{
    auto& ts = c.edit.tempoSequence;
    const auto beatsPerBar = (double) ts.getTimeSigAt (c.getPosition().getStart()).numerator.get();
    const auto length = te::BeatDuration::fromBeats (beatsPerBar * juce::jmax (1, bars));

    const auto startBeat = c.getStartBeat();
    c.setEnd (ts.toTime (startBeat + length), true);
    c.setLoopRangeBeats ({ 0_bp, length });
}

te::MidiNote* addNote (te::MidiClip& c, int pitch, te::BeatPosition start, te::BeatDuration length, int velocity)
{
    const auto clipLength = getClipLength (c);

    if (start < 0_bp || start >= te::toPosition (clipLength) || ! juce::isPositiveAndBelow (pitch, 128))
        return nullptr;

    length = juce::jmin (length, clipLength - te::toDuration (start));
    return c.getSequence().addNote (pitch, start, length, juce::jlimit (1, 127, velocity), 0, um (c));
}

te::MidiNote* findNote (te::MidiClip& c, int pitch, te::BeatPosition beat)
{
    // Dall'ultima: tra note sovrapposte vince quella disegnata sopra.
    auto& list = c.getSequence().getNotes();

    for (int i = list.size(); --i >= 0;)
    {
        auto n = list.getUnchecked (i);

        if (n->getNoteNumber() == pitch && beat >= n->getStartBeat() && beat < n->getEndBeat())
            return n;
    }

    return nullptr;
}

void moveNote (te::MidiClip& c, te::MidiNote& n, te::BeatPosition newStart, int newPitch)
{
    const auto clipLength = te::toPosition (getClipLength (c));
    const auto maxStart = clipLength - n.getLengthBeats();
    newStart = juce::jlimit (0_bp, juce::jmax (0_bp, maxStart), newStart);
    newPitch = juce::jlimit (0, 127, newPitch);

    if (newStart != n.getStartBeat())
        n.setStartAndLength (newStart, n.getLengthBeats(), um (c));

    if (newPitch != n.getNoteNumber())
        n.setNoteNumber (newPitch, um (c));
}

void resizeNote (te::MidiClip& c, te::MidiNote& n, te::BeatDuration newLength, te::BeatDuration minLength)
{
    const auto available = te::toPosition (getClipLength (c)) - n.getStartBeat();
    newLength = juce::jlimit (minLength, juce::jmax (minLength, available), newLength);

    if (newLength != n.getLengthBeats())
        n.setStartAndLength (n.getStartBeat(), newLength, um (c));
}

void setVelocity (te::MidiClip& c, te::MidiNote& n, int velocity)
{
    velocity = juce::jlimit (1, 127, velocity);

    if (velocity != n.getVelocity())
        n.setVelocity (velocity, um (c));
}

void deleteNote (te::MidiClip& c, te::MidiNote& n)
{
    c.getSequence().removeNote (n, um (c));
}

void quantise (te::MidiClip& c, te::BeatDuration grid)
{
    for (auto n : c.getSequence().getNotes())
        moveNote (c, *n, snapNearest (n->getStartBeat(), grid), n->getNoteNumber());
}

juce::String noteName (int pitch)
{
    return juce::MidiMessage::getMidiNoteName (pitch, true, true, 3);
}

} // namespace sonora::notes
