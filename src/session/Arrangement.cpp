/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Arrangement.h"
#include "Session.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace sonora::arrangement
{

juce::Array<te::Clip*> getClips (te::AudioTrack& t)
{
    juce::Array<te::Clip*> result;

    for (auto c : t.getClips())
        result.add (c);

    return result;
}

te::BeatDuration barLength (te::Edit& e)
{
    return te::BeatDuration::fromBeats ((double) e.tempoSequence.getTimeSigAt (0_tp).numerator.get());
}

double adaptiveGridBeats (double pixelsPerBeat, double beatsPerBar, double minPixels)
{
    if (pixelsPerBeat <= 0.0)
        return beatsPerBar;

    // Dal 1/32 in su: suddivisioni del beat, poi beat, battuta e gruppi di battute
    for (double beats : { 0.125, 0.25, 0.5, 1.0 })
        if (beats * pixelsPerBeat >= minPixels)
            return beats;

    for (double bars = 1.0; bars < 1024.0; bars *= 2.0)
        if (bars * beatsPerBar * pixelsPerBeat >= minPixels)
            return bars * beatsPerBar;

    return 1024.0 * beatsPerBar;
}

juce::String gridName (double beats, double beatsPerBar)
{
    if (beats >= beatsPerBar - 1.0e-9)
    {
        const int bars = juce::roundToInt (beats / beatsPerBar);
        return bars == 1 ? juce::String ("1 battuta") : juce::String (bars) + " battute";
    }

    // Un beat e' un quarto: 1 beat = 1/4, mezzo beat = 1/8...
    const int denominator = juce::roundToInt (4.0 / beats);
    return "1/" + juce::String (denominator);
}

te::MidiClip* createMidiClip (te::AudioTrack& t, te::BeatPosition start, int lengthInBars)
{
    auto& ts = t.edit.tempoSequence;
    start = juce::jmax (0_bp, start);
    const auto length = barLength (t.edit) * (double) juce::jmax (1, lengthInBars);
    auto clip = te::insertMIDIClip (t, "Clip MIDI", ts.toTime (te::BeatRange (start, length)));

    if (clip == nullptr)
        return nullptr;

    clip->setColour (t.getColour());
    return clip.get();
}

te::Clip* createClipFromFile (te::AudioTrack& t, te::BeatPosition start, const juce::File& file)
{
    auto& e = t.edit;
    auto& ts = e.tempoSequence;
    start = juce::jmax (0_bp, start);
    const auto startTime = ts.toTime (start);

    if (file.hasFileExtension (te::soundFileExtensions))
    {
        te::AudioFile audioFile (e.engine, file);

        if (! audioFile.isValid())
            return nullptr;

        auto clip = te::insertWaveClip (t, file.getFileNameWithoutExtension(), file,
                                        { { startTime, te::TimeDuration::fromSeconds (audioFile.getLength()) } },
                                        te::DeleteExistingClips::no);

        if (clip == nullptr)
            return nullptr;

        clip->setColour (t.getColour());
        return clip.get();
    }

    if (file.hasFileExtension (te::midiFileExtensions))
    {
        if (auto clip = te::createClipFromFile (file, t, te::MidiList::looksLikeMPEData (file)))
        {
            clip->setName (file.getFileNameWithoutExtension());
            clip->setStart (startTime, false, true);
            clip->setColour (t.getColour());
            return clip.get();
        }
    }

    return nullptr;
}

void moveClip (te::Clip& c, te::BeatPosition newStart, te::AudioTrack* newTrack)
{
    auto& ts = c.edit.tempoSequence;
    newStart = juce::jmax (0_bp, newStart);

    if (newTrack != nullptr && newTrack != c.getTrack())
        c.moveTo (*newTrack);

    if (newStart != c.getStartBeat())
        c.setStart (ts.toTime (newStart), false, true);
}

void resizeClip (te::Clip& c, te::BeatDuration newLength, te::BeatDuration minLength)
{
    auto& ts = c.edit.tempoSequence;
    newLength = juce::jmax (minLength, newLength);
    c.setEnd (ts.toTime (c.getStartBeat() + newLength), true);
}

te::Clip* duplicateClip (te::Clip& c)
{
    auto track = dynamic_cast<te::AudioTrack*> (c.getTrack());

    if (track == nullptr)
        return nullptr;

    auto state = c.state.createCopy();
    te::EditItemID::remapIDs (state, nullptr, c.edit);

    auto copy = te::insertClipWithState (*track, state);

    if (copy != nullptr)
        copy->setStart (c.edit.tempoSequence.toTime (c.getEndBeat()), false, true);

    return copy;
}

void deleteClip (te::Clip& c)
{
    c.removeFromParent();
}

bool isAnyTrackPlayingSession (te::Edit& e)
{
    for (auto t : te::getAudioTracks (e))
        if (t->playSlotClips.get())
            return true;

    return false;
}

void backToArrangement (te::Edit& e)
{
    session::stopAllClips (e);

    for (auto t : te::getAudioTracks (e))
        t->playSlotClips = false;
}

te::BeatPosition getEndOfContent (te::Edit& e, int minBars)
{
    auto end = te::BeatPosition() + barLength (e) * (double) minBars;

    for (auto t : te::getAudioTracks (e))
        for (auto c : t->getClips())
            end = juce::jmax (end, c->getEndBeat());

    return end;
}

} // namespace sonora::arrangement
