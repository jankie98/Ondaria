/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Recording.h"
#include "Session.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace sonora::recording
{

namespace
{
    const juce::Identifier inputProperty ("sonoraInput");
    const juce::Identifier armedProperty ("sonoraArmed");
    const juce::String audioPrefix ("audio:");

    bool isMidi (const te::InputDevice& d)
    {
        return d.getDeviceType() == te::InputDevice::physicalMidiDevice
            || d.getDeviceType() == te::InputDevice::virtualMidiDevice;
    }

    bool isAudio (const te::InputDevice& d)
    {
        return d.getDeviceType() == te::InputDevice::waveDevice;
    }

    /** True se l'ingresso della traccia corrisponde al dispositivo. */
    bool inputMatches (const juce::String& input, const te::InputDevice& d)
    {
        if (isMidi (d))
            return input == midiInputID;

        return isAudio (d) && input == audioPrefix + d.getName();
    }

    bool hasTarget (te::InputDeviceInstance& inst, te::EditItemID id)
    {
        for (auto dest : inst.destinations)
            if (dest->targetID == id)
                return true;

        return false;
    }

    te::InputDeviceInstance::Destination* destinationFor (te::InputDeviceInstance& inst, te::EditItemID id)
    {
        for (auto dest : inst.destinations)
            if (dest->targetID == id)
                return dest;

        return nullptr;
    }
}

//==============================================================================
std::vector<InputChoice> getInputChoices (te::Edit& e)
{
    std::vector<InputChoice> choices { { {}, "Nessun ingresso" }, { midiInputID, "Tutti gli ingressi MIDI" } };

    for (auto inst : e.getAllInputDevices())
        if (auto& d = inst->getInputDevice(); isAudio (d) && d.isEnabled())
            choices.push_back ({ audioPrefix + d.getName(), "Audio: " + d.getName() });

    return choices;
}

juce::String getInput (const te::AudioTrack& t)
{
    return t.state.getProperty (inputProperty, midiInputID).toString();
}

juce::String getInputName (const te::AudioTrack& t)
{
    const auto input = getInput (t);

    if (input.isEmpty())
        return "Nessun ingresso";

    if (input == midiInputID)
        return "MIDI";

    return input.fromFirstOccurrenceOf (audioPrefix, false, false);
}

void setInput (te::AudioTrack& t, const juce::String& inputID)
{
    t.state.setProperty (inputProperty, inputID, nullptr);
}

bool isExplicitlyArmed (const te::AudioTrack& t)
{
    return t.state.getProperty (armedProperty, false);
}

//==============================================================================
Recorder::Recorder (te::Edit& e)
    : edit (e)
{
    keyDispatcher->listeners.add (this);
    startTimerHz (30);
}

Recorder::~Recorder()
{
    keyDispatcher->listeners.remove (this);
    stopTimer();
}

//==============================================================================
const LiveTake* Recorder::getLiveTake (const te::AudioTrack& t) const
{
    auto it = liveTakes.find (t.itemID);
    return it != liveTakes.end() ? &it->second : nullptr;
}

void Recorder::midiKeyStateChanged (te::AudioTrack* t, const juce::Array<int>& on, const juce::Array<int>&,
                                    const juce::Array<int>& off)
{
    if (t != nullptr && &t->edit == &edit)
        notesReceived (*t, on, off);
}

void Recorder::notesReceived (te::AudioTrack& t, const juce::Array<int>& on, const juce::Array<int>& off)
{
    auto it = liveTakes.find (t.itemID);

    if (it == liveTakes.end())
        return;

    // Il motore raccoglie i tasti ogni poche decine di millisecondi: il tempo e' quello
    // della testina adesso, abbastanza preciso per un'anteprima
    const auto now = edit.getTransport().getPosition();
    auto& notes = it->second.notes;

    for (auto pitch : on)
        notes.push_back ({ pitch, now, now, true });

    for (auto pitch : off)
        for (auto n = notes.rbegin(); n != notes.rend(); ++n)
            if (n->held && n->pitch == pitch)
            {
                n->held = false;
                n->end = std::max (now, n->start);
                break;
            }
}

void Recorder::updateLiveTakes()
{
    const auto now = edit.getTransport().getPosition();
    std::map<te::EditItemID, LiveTake> active;

    for (auto t : te::getAudioTracks (edit))
    {
        // Bersaglio della registrazione: la traccia (arrangement) o uno dei suoi slot
        juce::Array<te::EditItemID> targets { t->itemID };

        for (auto slot : t->getClipSlotList().getClipSlots())
            if (slot != nullptr)
                targets.add (slot->itemID);

        for (auto inst : edit.getAllInputDevices())
        {
            if (inst->getInputDevice().isTrackDevice())
                continue;

            for (auto target : targets)
            {
                if (! inst->isRecording (target))
                    continue;

                const bool first = active.find (t->itemID) == active.end();
                auto& take = active[t->itemID];

                if (first)
                {
                    if (auto old = liveTakes.find (t->itemID); old != liveTakes.end())
                        take = std::move (old->second);
                    else
                        take.start = inst->getPunchInTime (target);
                }

                if (take.audio == nullptr)
                    if (auto f = inst->getRecordingFile (target); f != juce::File())
                        take.audio = edit.engine.getRecordingThumbnailManager().getThumbnailFor (f);

                for (auto& n : take.notes)
                    if (n.held)
                        n.end = std::max (now, n.start);
            }
        }
    }

    liveTakes = std::move (active);
}

void Recorder::setSelectedTrack (te::AudioTrack* t)
{
    const auto id = t != nullptr ? t->itemID : te::EditItemID();

    if (id == selectedTrackID)
        return;

    selectedTrackID = id;
    syncInputs();
}

bool Recorder::isAutoArmed (const te::AudioTrack& t) const
{
    return t.itemID == selectedTrackID && getInput (t) == midiInputID;
}

bool Recorder::isArmed (const te::AudioTrack& t) const
{
    // Una traccia congelata suona il suo file: non registra finche' non si scongela
    return getInput (t).isNotEmpty() && (isExplicitlyArmed (t) || isAutoArmed (t))
            && ! t.isFrozen (te::Track::anyFreeze);
}

void Recorder::setArmed (te::AudioTrack& t, bool armed)
{
    t.state.setProperty (armedProperty, armed, nullptr);
    syncInputs();
}

void Recorder::syncInputs()
{
    bool changed = false;
    auto tracks = te::getAudioTracks (edit);

    for (auto inst : edit.getAllInputDevices())
    {
        auto& device = inst->getInputDevice();

        if (device.isTrackDevice() || ! (isMidi (device) || isAudio (device)))
            continue;

        // Le tracce che devono ricevere questo ingresso. Il MIDI va anche alla
        // traccia selezionata, per suonarla anche quando non e' armata.
        std::vector<std::pair<te::AudioTrack*, bool>> wanted;

        for (auto t : tracks)
        {
            const auto input = getInput (*t);

            if (! inputMatches (input, device))
                continue;

            const bool armed = isArmed (*t);

            if (isMidi (device) && ! armed && t->itemID != selectedTrackID)
                continue;

            wanted.push_back ({ t, armed });
        }

        // Le destinazioni non si possono cambiare mentre l'ingresso registra.
        if (! inst->isRecording())
        {
            for (auto target : inst->getTargets())
            {
                if (te::findTrackForID (edit, target) == nullptr)
                    continue;

                const bool keep = std::any_of (wanted.begin(), wanted.end(),
                                               [target] (auto& w) { return w.first->itemID == target; });

                if (! keep)
                {
                    [[maybe_unused]] auto res = inst->removeTarget (target, nullptr);
                    changed = true;
                }
            }

            for (auto& [t, armed] : wanted)
            {
                if (! hasTarget (*inst, t->itemID))
                {
                    [[maybe_unused]] auto res = inst->setTarget (t->itemID, false, nullptr, 0);
                    changed = true;
                }
            }
        }

        for (auto& [t, armed] : wanted)
            if (auto dest = destinationFor (*inst, t->itemID); dest != nullptr && dest->recordEnabled.get() != armed)
                dest->recordEnabled = armed;
    }

    if (changed)
        edit.restartPlayback();
}

juce::Array<te::InputDeviceInstance*> Recorder::inputsFor (const te::AudioTrack& t) const
{
    juce::Array<te::InputDeviceInstance*> result;

    for (auto inst : edit.getAllInputDevices())
        if (! inst->getInputDevice().isTrackDevice() && hasTarget (*inst, t.itemID))
            result.add (inst);

    return result;
}

//==============================================================================
juce::Result Recorder::toggleArrangementRecording()
{
    auto& transport = edit.getTransport();

    if (transport.isRecording())
    {
        transport.stopRecording (false);
        return juce::Result::ok();
    }

    juce::Array<te::AudioTrack*> armed;

    for (auto t : te::getAudioTracks (edit))
        if (isArmed (*t))
            armed.add (t);

    if (armed.isEmpty())
        return juce::Result::fail ("Arma una traccia con il pulsante di registrazione per registrare");

    // Una traccia che suona la session tornerebbe a registrare in uno slot:
    // la riportiamo sulla linea del tempo.
    for (auto t : armed)
    {
        if (t->playSlotClips.get())
        {
            session::stopTrack (*t);
            t->playSlotClips = false;
        }
    }

    syncInputs();
    edit.dispatchPendingUpdatesSynchronously();
    transport.record (false);

    return transport.isRecording() ? juce::Result::ok()
                                   : juce::Result::fail ("La registrazione non e' partita: controlla gli ingressi");
}

bool Recorder::isRecordingInto (const te::AudioTrack& t) const
{
    for (auto inst : inputsFor (t))
        if (inst->isRecording (t.itemID))
            return true;

    return false;
}

std::optional<te::TimePosition> Recorder::getPunchInTime (const te::AudioTrack& t) const
{
    for (auto inst : inputsFor (t))
        if (inst->isRecording (t.itemID))
            return inst->getPunchInTime (t.itemID);

    return std::nullopt;
}

//==============================================================================
bool Recorder::canRecordInto (te::ClipSlot& slot) const
{
    auto t = dynamic_cast<te::AudioTrack*> (&slot.track);
    return t != nullptr && slot.getClip() == nullptr && isArmed (*t);
}

juce::Result Recorder::startSlotRecording (te::ClipSlot& slot)
{
    auto track = dynamic_cast<te::AudioTrack*> (&slot.track);

    if (track == nullptr)
        return juce::Result::fail ("Slot non valido");

    if (slot.getClip() != nullptr)
        return juce::Result::fail ("Lo slot contiene gia' una clip");

    if (getSlotRecordState (slot) != SlotRecordState::none)
        return juce::Result::ok();

    syncInputs();
    auto inputs = inputsFor (*track);

    if (inputs.isEmpty())
        return juce::Result::fail ("La traccia non ha un ingresso: sceglilo dal menu della traccia");

    auto& transport = edit.getTransport();
    te::TimePosition start;

    if (transport.isPlaying())
    {
        if (auto lp = session::nextLaunchPoint (edit))
            start = lp->unloopedTime;
        else
            start = transport.getPosition();
    }
    else
    {
        // Parte dall'inizio della battuta corrente, come il lancio di una clip.
        auto& ts = edit.tempoSequence;
        const auto barsBeats = ts.toBarsAndBeats (transport.getPosition());
        start = ts.toTime (te::tempo::BarsAndBeats { .bars = barsBeats.bars });
        session::startTransportFromBar (edit);
    }

    juce::StringArray errors;
    int started = 0;

    for (auto inst : inputs)
    {
        if (auto dest = inst->setTarget (slot.itemID, false, nullptr); ! dest)
        {
            errors.add (dest.error());
            continue;
        }

        te::InputDeviceInstance::RecordingParameters params;
        params.punchRange = { start, te::Edit::getMaximumEditTimeRange().getEnd() };
        params.targets = { slot.itemID };

        auto [contexts, prepareErrors] = te::extract (inst->prepareToRecord (params));
        errors.addArray (prepareErrors);

        if (! contexts.empty())
        {
            inst->startRecording (std::move (contexts));
            ++started;
        }
    }

    if (started == 0)
        return juce::Result::fail (errors.isEmpty() ? juce::String ("La registrazione non e' partita") : errors[0]);

    return juce::Result::ok();
}

void Recorder::stopSlotRecording (te::ClipSlot& slot)
{
    auto& transport = edit.getTransport();
    auto context = transport.getCurrentPlaybackContext();

    if (context == nullptr)
        return;

    const auto launchPoint = session::nextLaunchPoint (edit);
    const auto end = launchPoint ? launchPoint->unloopedTime : context->getUnloopedPosition();
    const auto launchAt = launchPoint ? std::optional (launchPoint->beat) : std::nullopt;

    for (auto inst : edit.getAllInputDevices())
    {
        if (! inst->isRecording (slot.itemID) || inst->isRecordingQueuedToStop (slot.itemID))
            continue;

        te::InputDeviceInstance::StopRecordingParameters params;
        params.targetsToStop = { slot.itemID };
        params.unloopedTimeToEndRecording = end;
        params.isLooping = false;
        params.markedRange = transport.getLoopRange();

        inst->stopRecording (params, [weakThis = juce::WeakReference<Recorder> (this), launchAt] (auto result)
        {
            if (weakThis == nullptr || ! result)
                return;

            for (auto clip : *result)
                if (clip != nullptr && clip->getClipSlot() != nullptr)
                    weakThis->finishSlotClip (*clip, launchAt, true);
        });
    }
}

Recorder::SlotRecordState Recorder::getSlotRecordState (te::ClipSlot& slot) const
{
    auto state = SlotRecordState::none;

    for (auto inst : edit.getAllInputDevices())
    {
        if (! inst->isRecording (slot.itemID))
            continue;

        if (inst->isRecordingQueuedToStop (slot.itemID))
            state = SlotRecordState::stopping;
        else
            return SlotRecordState::recording;
    }

    return state;
}

void Recorder::stopAllSlotRecordings()
{
    auto& transport = edit.getTransport();
    auto context = transport.getCurrentPlaybackContext();

    if (context == nullptr)
        return;

    const auto end = context->getUnloopedPosition();

    for (auto inst : edit.getAllInputDevices())
    {
        for (auto target : inst->getTargets())
        {
            if (te::findClipSlotForID (edit, target) == nullptr || ! inst->isRecording (target))
                continue;

            inst->prepareToStopRecording ({ target });

            te::InputDeviceInstance::StopRecordingParameters params;
            params.targetsToStop = { target };
            params.unloopedTimeToEndRecording = end;
            params.isLooping = false;
            params.markedRange = transport.getLoopRange();

            if (auto result = inst->stopRecording (params))
                for (auto clip : *result)
                    if (clip != nullptr && clip->getClipSlot() != nullptr)
                        finishSlotClip (*clip, std::nullopt, false);
        }
    }

    removeIdleSlotTargets();
}

bool Recorder::isRecording() const
{
    if (edit.getTransport().isRecording())
        return true;

    for (auto inst : edit.getAllInputDevices())
        if (inst->isRecording())
            return true;

    return false;
}

//==============================================================================
void Recorder::finishSlotClip (te::Clip& clip, std::optional<te::MonotonicBeat> launchAt, bool launch)
{
    auto slot = clip.getClipSlot();
    auto track = slot != nullptr ? dynamic_cast<te::AudioTrack*> (&slot->track) : nullptr;

    if (track == nullptr)
        return;

    if (auto audio = dynamic_cast<te::AudioClipBase*> (&clip))
        audio->setUsesProxy (false);
    else if (auto midi = dynamic_cast<te::MidiClip*> (&clip))
        midi->setUsesProxy (false);

    clip.setColour (track->getColour());

    if (auto wave = dynamic_cast<te::WaveAudioClip*> (&clip))
        wave->setAutoTempo (true);

    // La clip registrata va in loop su tutta la sua lunghezza, come le altre della session.
    clip.setLoopRangeBeats ({ 0_bp, clip.getLengthInBeats() });

    if (launch && edit.getTransport().isPlaying())
    {
        if (auto lh = clip.getLaunchHandle())
        {
            session::stopTrack (*track);
            track->playSlotClips = true;
            lh->play (std::nullopt);

            // La registrazione finisce a tempo ma la clip arriva qualche millesimo dopo:
            // quando parte la spostiamo avanti come se fosse partita in quel punto.
            if (launchAt)
                pendingAligns.push_back ({ clip.itemID, *launchAt, 30 });
        }
    }

    if (onSlotRecorded)
        onSlotRecorded (clip);
}

void Recorder::launchInPhase (te::Clip& clip, te::MonotonicBeat clipZero)
{
    auto slot = clip.getClipSlot();
    auto track = slot != nullptr ? dynamic_cast<te::AudioTrack*> (&slot->track) : nullptr;
    auto lh = clip.getLaunchHandle();

    if (track == nullptr || lh == nullptr || ! edit.getTransport().isPlaying())
        return;

    session::stopTrack (*track);
    track->playSlotClips = true;
    lh->play (std::nullopt);
    pendingAligns.push_back ({ clip.itemID, clipZero, 30, clip.getLoopLengthBeats() });
}

void Recorder::removeIdleSlotTargets()
{
    for (auto inst : edit.getAllInputDevices())
    {
        if (inst->isRecording())
            continue;

        for (auto target : inst->getTargets())
            if (te::findClipSlotForID (edit, target) != nullptr)
                [[maybe_unused]] auto res = inst->removeTarget (target, nullptr);
    }
}

void Recorder::timerCallback()
{
    updateLiveTakes();

    for (auto it = pendingAligns.begin(); it != pendingAligns.end();)
    {
        auto clip = te::findClipForID (edit, it->clipID);
        auto lh = clip != nullptr ? clip->getLaunchHandle() : nullptr;
        bool done = lh == nullptr || --it->ticksLeft <= 0;

        if (! done && lh->getPlayingStatus() == te::LaunchHandle::PlayState::playing)
        {
            if (auto played = lh->getPlayedMonotonicRange())
            {
                auto late = played->v.getStart() - it->beat.v;

                if (it->wrap > 0_bd)
                {
                    const double w = it->wrap.inBeats();
                    late = te::BeatDuration::fromBeats (std::fmod (std::fmod (late.inBeats(), w) + w, w));

                    if (late > 0_bd)
                        lh->nudge (late);
                }
                else if (late > 0_bd && late < 4_bd)
                {
                    lh->nudge (late);
                }
            }

            done = true;
        }

        it = done ? pendingAligns.erase (it) : it + 1;
    }

    // Tracce aggiunte o eliminate (anche con un undo): ricollega gli ingressi
    if (--ticksUntilSync <= 0)
    {
        ticksUntilSync = 15;
        syncInputs();
    }

    // Il trasporto si e' fermato mentre uno slot registrava
    if (! edit.getTransport().isPlaying())
        stopAllSlotRecordings();
    else
        removeIdleSlotTargets();
}

} // namespace sonora::recording
