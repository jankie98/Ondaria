/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later

    Il calcolo delle posizioni di lancio segue lo schema dell'esempio
    ClipLauncherDemo di Tracktion Engine (GPLv3+), riscritto per Sonora.
*/

#include "Session.h"
#include "Warp.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace sonora::session
{

namespace
{
    juce::UndoManager& undo (te::Edit& e)    { return e.getUndoManager(); }

    void beginTransaction (te::Edit& e, const juce::String& name)
    {
        undo (e).beginNewTransaction (name);
    }

    te::LaunchQuantisation& quantisationFor (te::Clip& c)
    {
        if (! c.usesGlobalLaunchQuatisation())
            if (auto lq = c.getLaunchQuantisation())
                return *lq;

        return c.edit.getLaunchQuantisation();
    }

    /** Distanza dal prossimo punto di quantizzazione, tenendo conto del loop del trasporto. */
    te::BeatDuration offsetToNextLaunch (const te::LaunchQuantisation& lq, te::BeatPosition pos,
                                         std::optional<te::BeatRange> loopRange)
    {
        auto next = pos;

        for (;;)
        {
            next = lq.getNext (next);

            if (next >= 0_bp)
                break;

            next = next + 0.001_bd;
        }

        if (! loopRange || next < loopRange->getEnd())
            return next - pos;

        next = lq.getNext (loopRange->getStart());

        if (! loopRange->contains (next))
            next = loopRange->getStart();

        if (loopRange->contains (pos))
            return (loopRange->getEnd() - pos) + (next - loopRange->getStart());

        return next - pos;
    }

    std::optional<te::MonotonicBeat> launchPosition (te::Edit& e, const te::LaunchQuantisation& lq)
    {
        auto& transport = e.getTransport();

        if (! transport.isPlaying())
            return std::nullopt;

        auto context = transport.getCurrentPlaybackContext();

        if (context == nullptr)
            return std::nullopt;

        auto syncPoint = context->getSyncPoint();

        if (! syncPoint)
            return std::nullopt;

        const auto loop = transport.looping.get() ? std::optional (e.tempoSequence.toBeats (transport.getLoopRange()))
                                                  : std::nullopt;

        return te::MonotonicBeat { syncPoint->monotonicBeat.v + offsetToNextLaunch (lq, syncPoint->beat, loop) };
    }

    /** Se il trasporto e' fermo lo fa partire dall'inizio della battuta corrente. */
    void startTransportIfStopped (te::Edit& e)
    {
        auto& transport = e.getTransport();

        if (transport.isPlaying())
            return;

        auto& ts = e.tempoSequence;
        const auto barsBeats = ts.toBarsAndBeats (transport.getPosition());
        transport.setPosition (ts.toTime (te::tempo::BarsAndBeats { .bars = barsBeats.bars }));
        transport.ensureContextAllocated();

        if (auto context = transport.getCurrentPlaybackContext())
            context->blockUntilSyncPointChange();
    }

    void playNow (te::Clip& c)
    {
        if (auto lh = c.getLaunchHandle())
            lh->play (launchPosition (c.edit, quantisationFor (c)));
    }

    void stopClip (te::Clip& c)
    {
        auto lh = c.getLaunchHandle();

        if (lh == nullptr)
            return;

        // Solo le clip che suonano o stanno per partire: le altre restano come sono.
        const bool active = lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing
                             || lh->getQueuedStatus() == te::LaunchHandle::QueueState::playQueued;

        if (active)
            lh->stop (launchPosition (c.edit, quantisationFor (c)));
    }

    void startTransport (te::Edit& e)
    {
        if (! e.getTransport().isPlaying())
            e.getTransport().play (false);
    }
}

//==============================================================================
juce::Colour colourForIndex (int index)
{
    static const juce::uint32 palette[] =
    {
        0xfff2a93b, 0xfff87171, 0xff7c9cff, 0xff4ade80, 0xffc084fc, 0xff22d3ee, 0xfffacc15, 0xfffb923c,
        0xfff472b6, 0xff34d399, 0xff60a5fa, 0xffa3e635, 0xffe879f9, 0xff2dd4bf, 0xfffca5a5, 0xff93c5fd
    };

    return juce::Colour (palette[(size_t) (index & 15)]);
}

void ensureGrid (te::Edit& e, int minScenes)
{
    auto& scenes = e.getSceneList();
    scenes.ensureNumberOfScenes (juce::jmax (minScenes, scenes.getNumScenes()));

    int i = 0;

    for (auto at : te::getAudioTracks (e))
    {
        at->getClipSlotList().ensureNumberOfSlots (scenes.getNumScenes());

        if (at->getColour().isTransparent())
            at->setColour (colourForIndex (i));

        ++i;
    }
}

te::AudioTrack* addTrack (te::Edit& e)
{
    beginTransaction (e, "Nuova traccia");

    const int index = te::getAudioTracks (e).size();
    auto track = e.insertNewAudioTrack (te::TrackInsertPoint (nullptr, te::getAudioTracks (e).getLast()), nullptr);

    if (track == nullptr)
        return nullptr;

    track->setName ("Traccia " + juce::String (index + 1));
    track->setColour (colourForIndex (index));
    track->getClipSlotList().ensureNumberOfSlots (e.getSceneList().getNumScenes());
    return track.get();
}

te::Scene* addScene (te::Edit& e)
{
    beginTransaction (e, "Nuova scena");
    auto& scenes = e.getSceneList();
    return scenes.insertScene (scenes.getNumScenes());
}

void deleteTrack (te::AudioTrack& track)
{
    auto& e = track.edit;
    beginTransaction (e, "Elimina traccia");
    stopTrack (track);
    e.deleteTrack (&track);
}

te::ClipSlot* getSlot (te::AudioTrack& track, int sceneIndex)
{
    return track.getClipSlotList().getClipSlots()[sceneIndex];
}

te::MidiClip* createMidiClip (te::ClipSlot& slot, int lengthInBars)
{
    auto& e = slot.edit;
    beginTransaction (e, "Nuova clip MIDI");

    deleteClip (slot);

    const auto beatsPerBar = (double) e.tempoSequence.getTimeSigAt (0_tp).numerator.get();
    const auto length = te::BeatDuration::fromBeats (beatsPerBar * juce::jmax (1, lengthInBars));
    const auto time = e.tempoSequence.toTime (te::BeatRange (0_bp, length));

    auto clip = te::insertMIDIClip (slot, "Clip MIDI", time);

    if (clip == nullptr)
        return nullptr;

    clip->setUsesProxy (false);
    clip->setLoopRangeBeats ({ 0_bp, length });
    clip->setColour (slot.track.getColour());
    return clip.get();
}

te::Clip* createClipFromFile (te::ClipSlot& slot, const juce::File& file)
{
    auto& e = slot.edit;

    if (file.hasFileExtension (te::soundFileExtensions))
    {
        te::AudioFile audioFile (e.engine, file);

        if (! audioFile.isValid())
            return nullptr;

        beginTransaction (e, "Importa audio");

        auto clip = te::insertWaveClip (slot, file.getFileNameWithoutExtension(), file,
                                        { { 0_tp, te::TimeDuration::fromSeconds (audioFile.getLength()) } },
                                        te::DeleteExistingClips::yes);

        if (clip == nullptr)
            return nullptr;

        clip->setUsesProxy (false);
        warp::prepareImported (*clip);

        // Nella session le clip girano in loop: sulle battute se seguono il tempo
        if (clip->getAutoTempo())
            clip->setLoopRangeBeats ({ 0_bp, clip->getLengthInBeats() });
        else
            clip->setLoopRange ({ 0_tp, clip->getPosition().getLength() });
        clip->setColour (slot.track.getColour());
        return clip.get();
    }

    if (file.hasFileExtension (te::midiFileExtensions))
    {
        beginTransaction (e, "Importa MIDI");
        deleteClip (slot);

        if (auto clip = te::createClipFromFile (file, slot, te::MidiList::looksLikeMPEData (file)))
        {
            clip->setUsesProxy (false);
            clip->setName (file.getFileNameWithoutExtension());
            clip->setLoopRangeBeats (clip->getEditBeatRange());
            clip->setColour (slot.track.getColour());
            return clip.get();
        }
    }

    return nullptr;
}

void deleteClip (te::ClipSlot& slot)
{
    if (auto clip = slot.getClip())
    {
        stopClip (*clip);
        clip->removeFromParent();
    }
}

void launchClip (te::Clip& clip)
{
    // Una traccia congelata suona solo il suo file: gli slot restano fermi
    if (auto at = dynamic_cast<te::AudioTrack*> (clip.getTrack()); at != nullptr && at->isFrozen (te::Track::individualFreeze))
        return;

    auto& e = clip.edit;
    startTransportIfStopped (e);

    if (auto at = dynamic_cast<te::AudioTrack*> (clip.getTrack()))
        for (auto slot : at->getClipSlotList().getClipSlots())
            if (auto other = slot != nullptr ? slot->getClip() : nullptr; other != nullptr && other != &clip)
                stopClip (*other);

    playNow (clip);
    startTransport (e);
}

void launchScene (te::Edit& e, int sceneIndex)
{
    startTransportIfStopped (e);

    for (auto at : te::getAudioTracks (e))
    {
        auto slots = at->getClipSlotList().getClipSlots();
        auto target = slots[sceneIndex] != nullptr ? slots[sceneIndex]->getClip() : nullptr;

        // Slot vuoto (o traccia congelata): la traccia continua con quello che sta suonando.
        if (target == nullptr || at->isFrozen (te::Track::individualFreeze))
            continue;

        for (auto slot : slots)
            if (auto c = slot != nullptr ? slot->getClip() : nullptr; c != nullptr && c != target)
                stopClip (*c);

        playNow (*target);
    }

    startTransport (e);
}

void stopTrack (te::AudioTrack& track)
{
    for (auto slot : track.getClipSlotList().getClipSlots())
        if (auto c = slot != nullptr ? slot->getClip() : nullptr)
            stopClip (*c);
}

void stopAllClips (te::Edit& e)
{
    for (auto at : te::getAudioTracks (e))
        stopTrack (*at);
}

SlotState getSlotState (te::ClipSlot& slot)
{
    auto clip = slot.getClip();

    if (clip == nullptr)
        return SlotState::empty;

    auto lh = clip->getLaunchHandle();

    if (lh == nullptr)
        return SlotState::stopped;

    if (auto queued = lh->getQueuedStatus())
        return *queued == te::LaunchHandle::QueueState::playQueued ? SlotState::playQueued : SlotState::stopQueued;

    return lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing ? SlotState::playing : SlotState::stopped;
}

std::optional<float> getPlayProgress (te::Clip& clip)
{
    auto lh = clip.getLaunchHandle();

    if (lh == nullptr || lh->getPlayingStatus() != te::LaunchHandle::PlayState::playing)
        return std::nullopt;

    auto played = lh->getPlayedRange();

    if (! played)
        return std::nullopt;

    auto handle = clip.isLooping()
                    ? te::LauncherClipPlaybackHandle::forLooping (clip.getLoopRangeBeats(), clip.getOffsetInBeats())
                    : te::LauncherClipPlaybackHandle::forOneShot ({ te::toPosition (clip.getOffsetInBeats()), clip.getLengthInBeats() });

    handle.start (played->getStart());
    return handle.getProgress (played->getEnd());
}

std::optional<LaunchPoint> nextLaunchPoint (te::Edit& e)
{
    auto& transport = e.getTransport();

    if (! transport.isPlaying())
        return std::nullopt;

    auto context = transport.getCurrentPlaybackContext();

    if (context == nullptr)
        return std::nullopt;

    auto syncPoint = context->getSyncPoint();

    if (! syncPoint)
        return std::nullopt;

    auto& ts = e.tempoSequence;
    const auto loop = transport.looping.get() ? std::optional (ts.toBeats (transport.getLoopRange()))
                                              : std::nullopt;
    const auto offset = offsetToNextLaunch (e.getLaunchQuantisation(), syncPoint->beat, loop);
    const auto timeOffset = ts.toTime (syncPoint->beat + offset) - ts.toTime (syncPoint->beat);

    return LaunchPoint { te::MonotonicBeat { syncPoint->monotonicBeat.v + offset },
                         syncPoint->unloopedTime + te::TimeDuration::fromSeconds (std::max (0.0, timeOffset.inSeconds())) };
}

void startTransportFromBar (te::Edit& e)
{
    startTransportIfStopped (e);
    startTransport (e);
}

std::unique_ptr<te::Edit> loadEdit (te::Engine& engine, const juce::File& file)
{
    // L'id sta come attributo della radice: basta leggere l'inizio del file
    te::ProjectItemID id;

    if (auto root = juce::XmlDocument (file).getDocumentElementIfTagMatches ("EDIT"))
        id = te::ProjectItemID (root->getStringAttribute ("projectID"));

    if (! id.isValid())
        id = te::ProjectItemID::createNewID ({});

    auto state = te::loadEditFromFile (engine, file, id);

    if (! state.isValid())
        return {};

    return te::Edit::createEdit ({ engine, state, id, te::Edit::forEditing, nullptr,
                                   te::Edit::getDefaultNumUndoLevels(), [file] { return file; }, {} });
}

void setUpFilePaths (te::Edit& e)
{
    // Il motore scrive i percorsi con File::getRelativePathFrom (fileDelProgetto),
    // cioe' trattando il file come una cartella ("../campione.wav"); il risolutore
    // predefinito invece li cerca accanto al file e sale di una cartella di troppo.
    e.filePathResolver = [&e] (const juce::String& path) -> juce::File
    {
        if (path.isEmpty())
            return {};

        if (juce::File::isAbsolutePath (path))
            return path;

        if (e.editFileRetriever)
        {
            if (auto editFile = e.editFileRetriever(); editFile != juce::File())
            {
                if (auto f = editFile.getChildFile (path); f.existsAsFile())
                    return f;

                if (auto f = editFile.getSiblingFile (path); f.existsAsFile())
                    return f;

                return editFile.getChildFile (path);
            }
        }

        return juce::File::getCurrentWorkingDirectory().getChildFile (path);
    };
}

juce::StringArray getQuantisationNames()
{
    return { "Nessuna", "8 battute", "4 battute", "2 battute", "1 battuta",
             "1/2 T", "1/2", "1/2 P", "1/4 T", "1/4", "1/4 P", "1/8 T", "1/8", "1/8 P",
             "1/16 T", "1/16", "1/16 P", "1/32 T", "1/32", "1/32 P", "1/64 T", "1/64", "1/64 P" };
}

} // namespace sonora::session
