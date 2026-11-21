/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Capture.h"
#include "Arrangement.h"
#include "Notes.h"
#include "Recording.h"
#include "Session.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace sonora::capture
{

namespace
{
    constexpr double midiKeepSeconds = 600.0;     // quanto MIDI teniamo in memoria
    constexpr double midiGapSeconds = 6.0;        // una pausa piu' lunga separa due idee
    constexpr double audioGapSeconds = 2.5;
    constexpr float loudThreshold = 0.004f;       // circa -48 dBFS
    constexpr double mappingTolerance = 0.1;      // secondi oltre i bordi di un tratto
    constexpr int maxBars = 64;

    juce::String audioDeviceName (const te::AudioTrack& t)
    {
        const auto input = recording::getInput (t);
        return input.startsWith ("audio:") ? input.fromFirstOccurrenceOf ("audio:", false, false) : juce::String();
    }

    te::ClipSlot* firstFreeSlot (te::AudioTrack& t)
    {
        session::ensureGrid (t.edit);

        for (int i = 0;; ++i)
        {
            auto slot = session::getSlot (t, i);

            if (slot == nullptr)
            {
                session::addScene (t.edit);
                return session::getSlot (t, i);
            }

            if (slot->getClip() == nullptr)
                return slot;
        }
    }

    int barsFor (double spanBeats, double barBeats)
    {
        int bars = 1;

        while (bars * barBeats < spanBeats - 0.01 && bars < maxBars)
            bars *= 2;

        return bars;
    }
}

//==============================================================================
/** Dove finisce la clip e come si passa da un campione del dispositivo a un beat della clip. */
struct Capturer::Placement
{
    bool inTime = false;                 // il trasporto andava: la clip e' allineata al progetto
    te::BeatPosition clipStart;          // inizio nel progetto (solo se inTime)
    int bars = 1;
    te::BeatDuration length;
    double bpm = 120.0;
    int64_t zeroPosition = 0;            // campione del dispositivo che cade sul beat 0 della clip
    int64_t lengthSamples = 0;
    std::function<double (int64_t)> beatInClip;
};

//==============================================================================
Capturer::Capturer (te::Engine& e, double seconds) : engine (e), audioSeconds (seconds)
{
    for (auto& l : lastLoud)
        l = -1;

    engine.getDeviceManager().deviceManager.addAudioCallback (this);
    attachMidiInputs();
    startTimerHz (30);
}

Capturer::~Capturer()
{
    stopTimer();
    engine.getDeviceManager().deviceManager.removeAudioCallback (this);

    for (auto& d : listenedInputs)
        if (auto p = dynamic_cast<te::PhysicalMidiInputDevice*> (d.get()))
            p->removeListener (this);
}

void Capturer::setEdit (te::Edit* e)
{
    edit = e;
    segments.clear();
    wasPlaying = false;
}

void Capturer::attachMidiInputs()
{
    for (auto& d : engine.getDeviceManager().getMidiInDevices())
    {
        auto p = dynamic_cast<te::PhysicalMidiInputDevice*> (d.get());

        if (p == nullptr || std::find (listenedInputs.begin(), listenedInputs.end(), d) != listenedInputs.end())
            continue;

        p->addListener (this);
        listenedInputs.push_back (d);
    }
}

//==============================================================================
void Capturer::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    running = false;
    sampleRate = device->getCurrentSampleRate() > 0 ? device->getCurrentSampleRate() : 44100.0;

    const int channels = juce::jlimit (0, maxChannels, device->getActiveInputChannels().countNumberOfSetBits());
    const int length = (int) (audioSeconds * sampleRate.load());
    ring.setSize (juce::jmax (1, channels), juce::jmax (1, length), false, true, false);
    ring.clear();

    // Il dispositivo e' cambiato: quello che c'era prima non vale piu'
    devicePosition = 0;
    lastCallbackPosition = 0;
    lastCallbackMs = juce::Time::getMillisecondCounterHiRes();

    for (auto& l : lastLoud)
        l = -1;

    {
        const juce::ScopedLock sl (midiLock);
        midiEvents.clear();
        midiConsumedUpTo = -1;
    }

    audioConsumedUpTo.clear();
    segments.clear();
    running = true;
}

void Capturer::audioDeviceStopped()
{
    running = false;
}

void Capturer::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                 float* const* outputs, int numOutputs, int numSamples,
                                                 const juce::AudioIODeviceCallbackContext&)
{
    // Non suoniamo nulla: le uscite restano mute (il motore suona per conto suo)
    for (int ch = 0; ch < numOutputs; ++ch)
        if (outputs[ch] != nullptr)
            juce::FloatVectorOperations::clear (outputs[ch], numSamples);

    const auto pos = devicePosition.load();

    if (running)
    {
        const int capacity = ring.getNumSamples();
        const int channels = juce::jmin (numInputs, ring.getNumChannels());

        for (int ch = 0; ch < channels; ++ch)
        {
            if (inputs[ch] == nullptr)
                continue;

            const auto range = juce::FloatVectorOperations::findMinAndMax (inputs[ch], numSamples);

            if (juce::jmax (std::abs (range.getStart()), std::abs (range.getEnd())) > loudThreshold)
                lastLoud[(size_t) ch] = pos + numSamples;

            int written = 0;

            while (written < numSamples)
            {
                const int index = (int) ((pos + written) % capacity);
                const int n = juce::jmin (numSamples - written, capacity - index);
                ring.copyFrom (ch, index, inputs[ch] + written, n);
                written += n;
            }
        }
    }

    lastCallbackPosition = pos + numSamples;
    lastCallbackMs = juce::Time::getMillisecondCounterHiRes();
    devicePosition = pos + numSamples;
}

//==============================================================================
int64_t Capturer::positionNow() const
{
    // Senza dispositivo audio (nessuna scheda) contiamo col tempo reale
    if (! running)
        return (int64_t) ((juce::Time::getMillisecondCounterHiRes() - createdMs) * sampleRate.load() / 1000.0);

    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - lastCallbackMs.load();
    // Tra un blocco e l'altro avanziamo col tempo reale, ma mai oltre un blocco abbondante
    const auto ahead = (int64_t) (juce::jlimit (0.0, 50.0, elapsedMs) * sampleRate.load() / 1000.0);
    return lastCallbackPosition.load() + ahead;
}

void Capturer::handleIncomingMidiMessage (const juce::MidiMessage& m)
{
    addLiveMidi (m);
}

void Capturer::addLiveMidi (const juce::MidiMessage& m)
{
    addMidiAt (m, positionNow());
}

void Capturer::addMidiAt (const juce::MidiMessage& m, int64_t position)
{
    if (! m.isNoteOnOrOff())
        return;

    const juce::ScopedLock sl (midiLock);
    midiEvents.push_back ({ position, m });
}

//==============================================================================
void Capturer::timerCallback()
{
    updateTransportMapping();

    // Tieni solo gli ultimi minuti di MIDI
    const auto oldest = devicePosition.load() - (int64_t) (midiKeepSeconds * sampleRate.load());

    {
        const juce::ScopedLock sl (midiLock);
        auto it = std::find_if (midiEvents.begin(), midiEvents.end(), [oldest] (auto& ev) { return ev.position >= oldest; });
        midiEvents.erase (midiEvents.begin(), it);
    }

    // Controller collegati dopo l'avvio
    if (++ticks % 30 == 0)
        attachMidiInputs();
}

void Capturer::updateTransportMapping()
{
    if (edit == nullptr)
        return;

    auto& transport = edit->getTransport();
    auto context = transport.getCurrentPlaybackContext();

    if (! transport.isPlaying() || context == nullptr)
    {
        wasPlaying = false;
        return;
    }

    // Lettura coerente: se nel frattempo e' passato un blocco, riproviamo al prossimo giro
    const auto before = devicePosition.load();
    const double editSeconds = context->getPosition().inSeconds();

    if (devicePosition.load() != before)
        return;

    const double sr = sampleRate.load();
    const auto offset = (int64_t) std::llround (editSeconds * sr) - before;
    const auto tolerance = (int64_t) (0.05 * sr);

    if (! wasPlaying || segments.empty() || std::abs (offset - segments.back().offset) > tolerance)
    {
        // Partenza o salto (loop, riposizionamento): comincia un tratto nuovo
        segments.push_back ({ before, before, offset, { offset } });

        if (segments.size() > 256)
            segments.erase (segments.begin());
    }
    else
    {
        auto& s = segments.back();
        s.end = before;
        s.offsets.push_back (offset);

        if (s.offsets.size() > 31)
            s.offsets.erase (s.offsets.begin());

        auto sorted = s.offsets;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        s.offset = sorted[sorted.size() / 2];

    }

    wasPlaying = true;
}

std::optional<int64_t> Capturer::getTransportOffset() const
{
    if (! wasPlaying || segments.empty())
        return std::nullopt;

    return segments.back().offset;
}

std::optional<double> Capturer::editTimeFor (int64_t position) const
{
    const double sr = sampleRate.load();
    const auto tolerance = (int64_t) (mappingTolerance * sr);

    for (auto it = segments.rbegin(); it != segments.rend(); ++it)
    {
        const bool isLast = it == segments.rbegin();
        const auto end = isLast && wasPlaying ? std::numeric_limits<int64_t>::max() : it->end + tolerance;

        if (position >= it->start - tolerance && position <= end)
            return (double) (position + it->offset) / sr;
    }

    return std::nullopt;
}

std::optional<Capturer::Placement> Capturer::place (te::AudioTrack& track, int64_t start, int64_t end, double latency) const
{
    auto& e = track.edit;
    auto& ts = e.tempoSequence;
    const double sr = sampleRate.load();
    const double bar = arrangement::barLength (e).inBeats();

    Placement p;

    // Il trasporto andava per tutta la frase: la clip va dove l'hai suonata
    const auto t0 = editTimeFor (start);
    const auto t1 = editTimeFor (end);

    if (t0 && t1 && *t1 > *t0)
    {
        const auto offset = (int64_t) std::llround (*t0 * sr) - start;
        auto beatAt = [&ts, offset, sr, latency] (int64_t pos)
        {
            return ts.toBeats (te::TimePosition::fromSeconds ((double) (pos + offset) / sr - latency)).inBeats();
        };

        const double first = beatAt (start), last = beatAt (end);
        // Chi entra un po' in anticipo (fino a un sedicesimo) appartiene alla battuta dopo
        const double clipStart = juce::jmax (0.0, std::floor ((first + 0.25) / bar) * bar);

        p.inTime = true;
        p.clipStart = te::BeatPosition::fromBeats (clipStart);
        p.bars = barsFor (last - clipStart, bar);
        p.length = te::BeatDuration::fromBeats (p.bars * bar);
        p.bpm = ts.getTempoAt (ts.toTime (p.clipStart)).getBpm();

        const double startSeconds = ts.toTime (p.clipStart).inSeconds() + latency;
        const double endSeconds = ts.toTime (p.clipStart + p.length).inSeconds() + latency;
        p.zeroPosition = (int64_t) std::llround (startSeconds * sr) - offset;
        p.lengthSamples = (int64_t) std::llround ((endSeconds - startSeconds) * sr);
        p.beatInClip = [beatAt, clipStart] (int64_t pos) { return beatAt (pos) - clipStart; };
        return p;
    }

    // Trasporto fermo: un loop nuovo al tempo del progetto, a partire dalla prima nota
    p.bpm = ts.getTempoAt (e.getTransport().getPosition()).getBpm();
    const double beatsPerSample = p.bpm / 60.0 / sr;
    p.bars = barsFor ((double) (end - start) * beatsPerSample, bar);
    p.length = te::BeatDuration::fromBeats (p.bars * bar);
    p.zeroPosition = start;
    p.lengthSamples = (int64_t) std::llround (p.length.inBeats() / beatsPerSample);
    p.beatInClip = [start, beatsPerSample] (int64_t pos) { return (double) (pos - start) * beatsPerSample; };
    return p;
}

//==============================================================================
bool Capturer::hasSomethingFor (const te::AudioTrack& t) const
{
    const auto input = recording::getInput (t);

    if (input == recording::midiInputID)
    {
        const juce::ScopedLock sl (midiLock);

        for (auto& ev : midiEvents)
            if (ev.position > midiConsumedUpTo && ev.message.isNoteOn())
                return true;

        return false;
    }

    const auto name = audioDeviceName (t);

    if (name.isEmpty())
        return false;

    for (auto* dev : engine.getDeviceManager().getWaveInputDevices())
    {
        if (dev->getName() != name)
            continue;

        const auto consumedIt = audioConsumedUpTo.find (name);
        const auto consumed = consumedIt != audioConsumedUpTo.end() ? consumedIt->second : -1;
        const auto oldest = devicePosition.load() - ring.getNumSamples();
        const auto& chans = dev->getChannels();

        for (size_t i = 0; i < chans.size(); ++i)
            if (const int ch = chans[i].indexInDevice; ch >= 0 && ch < maxChannels)
                if (const auto loud = lastLoud[(size_t) ch].load(); loud > consumed && loud > oldest)
                    return true;
    }

    return false;
}

juce::Result Capturer::captureArmedTracks (recording::Recorder& recorder, Destination dest, juce::Array<te::Clip*>* created)
{
    if (edit == nullptr)
        return juce::Result::fail ("Nessun progetto aperto");

    updateTransportMapping();

    juce::StringArray problems;
    int captured = 0;
    const auto midiCut = positionNow();
    batch = true;

    for (auto t : te::getAudioTracks (*edit))
    {
        if (! recorder.isArmed (*t))
            continue;

        te::Clip* clip = nullptr;

        if (auto res = captureTrack (*t, dest, &recorder, &clip); res.failed())
            problems.add (res.getErrorMessage());
        else if (clip != nullptr)
        {
            ++captured;

            if (created != nullptr)
                created->add (clip);
        }
    }

    // Le note catturate non si ricatturano (anche con piu' tracce armate sullo stesso ingresso)
    batch = false;

    if (captured > 0)
    {
        const juce::ScopedLock sl (midiLock);
        midiConsumedUpTo = juce::jmax (midiConsumedUpTo, midiCut);
    }

    if (captured > 0)
        return juce::Result::ok();

    if (problems.isEmpty())
        return juce::Result::fail ("Nessuna traccia armata: arma una traccia (o selezionane una MIDI) e riprova");

    return juce::Result::fail (problems[0]);
}

juce::Result Capturer::captureTrack (te::AudioTrack& t, Destination dest, recording::Recorder* recorder, te::Clip** created)
{
    if (edit == nullptr)
        edit = &t.edit;

    updateTransportMapping();

    const auto input = recording::getInput (t);

    if (input == recording::midiInputID)
        return captureMidi (t, dest, recorder, created);

    const auto name = audioDeviceName (t);

    for (auto* dev : engine.getDeviceManager().getWaveInputDevices())
        if (dev->getName() == name)
            return captureAudio (t, *dev, dest, recorder, created);

    return juce::Result::fail (t.getName() + ": nessun ingresso da cui catturare");
}

//==============================================================================
std::vector<Capturer::CapturedNote> Capturer::collectNotes() const
{
    std::vector<CapturedNote> notes;
    std::map<int, size_t> open;   // canale * 128 + nota -> nota aperta
    const auto now = positionNow();

    const juce::ScopedLock sl (midiLock);

    for (auto& ev : midiEvents)
    {
        if (ev.position <= midiConsumedUpTo)
            continue;

        const auto& m = ev.message;
        const int key = m.getChannel() * 128 + m.getNoteNumber();

        if (m.isNoteOn())
        {
            if (auto it = open.find (key); it != open.end())
                notes[it->second].end = ev.position;

            open[key] = notes.size();
            notes.push_back ({ ev.position, -1, m.getNoteNumber(), m.getVelocity() });
        }
        else if (auto it = open.find (key); it != open.end())
        {
            notes[it->second].end = ev.position;
            open.erase (it);
        }
    }

    // Note ancora tenute: finiscono adesso
    for (auto& n : notes)
        if (n.end < 0)
            n.end = juce::jmax (n.start + 1, now);

    return notes;
}

juce::Result Capturer::captureMidi (te::AudioTrack& t, Destination dest, recording::Recorder* recorder, te::Clip** created)
{
    auto notes = collectNotes();

    if (notes.empty())
        return juce::Result::fail ("Niente da catturare: suona qualcosa e riprova");

    // L'ultima idea suonata: le note dopo l'ultima pausa lunga
    const auto gap = (int64_t) (midiGapSeconds * sampleRate.load());
    size_t first = 0;
    int64_t maxEnd = notes[0].end;

    for (size_t i = 1; i < notes.size(); ++i)
    {
        if (notes[i].start - maxEnd > gap)
            first = i;

        maxEnd = juce::jmax (maxEnd, notes[i].end);
    }

    notes.erase (notes.begin(), notes.begin() + (long) first);

    int64_t start = notes.front().start, end = notes.front().end;

    for (auto& n : notes)
        end = juce::jmax (end, n.end);

    const double latency = engine.getDeviceManager().getOutputLatencySeconds();
    auto placement = place (t, start, end, latency);

    if (! placement)
        return juce::Result::fail ("Impossibile allineare la cattura");

    auto& e = t.edit;
    e.getUndoManager().beginNewTransaction ("Cattura MIDI");

    te::MidiClip* clip = nullptr;

    if (dest == Destination::session)
    {
        if (auto slot = firstFreeSlot (t))
            clip = session::createMidiClip (*slot, placement->bars);
    }
    else
    {
        auto at = placement->clipStart;

        if (! placement->inTime)
        {
            const double bar = arrangement::barLength (e).inBeats();
            const double playhead = e.tempoSequence.toBeats (e.getTransport().getPosition()).inBeats();
            at = te::BeatPosition::fromBeats (std::floor (playhead / bar) * bar);
        }

        clip = arrangement::createMidiClip (t, at, placement->bars);
    }

    if (clip == nullptr)
        return juce::Result::fail ("Impossibile creare la clip");

    clip->setName ("Cattura");
    const double length = placement->length.inBeats();
    constexpr double minNote = 1.0 / 32.0;

    for (auto& n : notes)
    {
        const double s = juce::jlimit (0.0, length - minNote, placement->beatInClip (n.start));
        const double l = juce::jlimit (minNote, length - s, placement->beatInClip (n.end) - s);
        notes::addNote (*clip, n.pitch, te::BeatPosition::fromBeats (s), te::BeatDuration::fromBeats (l), n.velocity);
    }

    // Nella session la clip continua a suonare in fase, se il brano sta andando
    if (dest == Destination::session && placement->inTime && recorder != nullptr && e.getTransport().isPlaying())
    {
        if (auto context = e.getTransport().getCurrentPlaybackContext())
        {
            if (auto sync = context->getSyncPoint())
            {
                const auto zero = te::MonotonicBeat { sync->monotonicBeat.v - (sync->beat - placement->clipStart) };
                recorder->launchInPhase (*clip, zero);
            }
        }
    }

    if (! batch)
    {
        const juce::ScopedLock sl (midiLock);
        midiConsumedUpTo = juce::jmax (midiConsumedUpTo, end);
    }

    if (created != nullptr)
        *created = clip;

    return juce::Result::ok();
}

juce::Result Capturer::captureAudio (te::AudioTrack& t, te::WaveInputDevice& dev, Destination dest,
                                     recording::Recorder* recorder, te::Clip** created)
{
    const double sr = sampleRate.load();
    const auto now = devicePosition.load();
    const int capacity = ring.getNumSamples();
    const auto name = dev.getName();

    std::vector<int> channels;
    const auto& chans = dev.getChannels();

    for (size_t i = 0; i < chans.size(); ++i)
        if (const int ch = chans[i].indexInDevice; ch >= 0 && ch < ring.getNumChannels())
            channels.push_back (ch);

    if (channels.empty() || capacity <= 1)
        return juce::Result::fail ("L'ingresso " + name + " non e' attivo");

    auto consumedIt = audioConsumedUpTo.find (name);
    const auto from = juce::jmax (now - (int64_t) capacity + 1, consumedIt != audioConsumedUpTo.end() ? consumedIt->second : (int64_t) 0);

    // Dove c'e' suono, a blocchi da ~10 ms
    const int chunk = juce::jmax (64, (int) (sr / 100.0));
    std::vector<bool> loud;

    for (auto pos = from; pos + chunk <= now; pos += chunk)
    {
        float peak = 0.0f;

        for (int ch : channels)
        {
            const int index = (int) (pos % capacity);
            const int n = juce::jmin (chunk, capacity - index);
            peak = juce::jmax (peak, ring.getMagnitude (ch, index, n));

            if (n < chunk)
                peak = juce::jmax (peak, ring.getMagnitude (ch, 0, chunk - n));
        }

        loud.push_back (peak > loudThreshold);
    }

    auto last = std::find (loud.rbegin(), loud.rend(), true);

    if (last == loud.rend())
        return juce::Result::fail ("Niente da catturare su " + name + ": suona qualcosa e riprova");

    const auto endChunk = (int64_t) (loud.rend() - last);   // primo blocco dopo l'ultimo suono
    const auto gapChunks = (int64_t) (audioGapSeconds * sr / chunk);
    int64_t startChunk = 0;
    int64_t silent = 0;

    for (auto i = endChunk - 1; i >= 0; --i)
    {
        if (loud[(size_t) i])
        {
            startChunk = i;
            silent = 0;
        }
        else if (++silent >= gapChunks)
        {
            break;
        }
    }

    const int64_t phraseStart = from + startChunk * chunk;
    const int64_t phraseEnd = from + endChunk * chunk;
    const double latency = (double) engine.getDeviceManager().getRecordAdjustmentSamples() / sr;

    auto placement = place (t, phraseStart, phraseEnd, latency);

    if (! placement)
        return juce::Result::fail ("Impossibile allineare la cattura");

    // Senza tempo di riferimento teniamo un attimo prima dell'attacco
    if (! placement->inTime)
        placement->zeroPosition = juce::jmax ((int64_t) 0, placement->zeroPosition - (int64_t) (0.01 * sr));

    // Scrive il file: quello che manca (prima dell'inizio dell'anello o dopo adesso) e' silenzio
    auto& e = t.edit;
    auto editFile = te::EditFileOperations (e).getEditFile();
    auto folder = editFile != juce::File() && editFile.getParentDirectory().isDirectory()
                    ? editFile.getParentDirectory().getChildFile ("Catture")
                    : engine.getTemporaryFileManager().getTempDirectory().getChildFile ("Catture");
    folder.createDirectory();
    auto file = folder.getNonexistentChildFile (juce::File::createLegalFileName (t.getName() + " cattura"), ".wav", false);

    {
        te::AudioFileWriter writer (te::AudioFile (engine, file), engine.getAudioFileFormatManager().getWavFormat(),
                                    (int) channels.size(), sr, 24, {}, 0);

        if (! writer.isOpen())
            return juce::Result::fail ("Impossibile scrivere " + file.getFullPathName());

        const int block = 4096;
        juce::AudioBuffer<float> buffer ((int) channels.size(), block);
        const auto oldest = now - capacity + 1;

        for (int64_t done = 0; done < placement->lengthSamples; done += block)
        {
            const int n = (int) juce::jmin ((int64_t) block, placement->lengthSamples - done);
            buffer.clear();

            for (int i = 0; i < n; ++i)
            {
                const auto pos = placement->zeroPosition + done + i;

                if (pos < oldest || pos >= now || pos < 0)
                    continue;

                const int index = (int) (pos % capacity);

                for (size_t c = 0; c < channels.size(); ++c)
                    buffer.setSample ((int) c, i, ring.getSample (channels[c], index));
            }

            writer.appendBuffer (buffer, n);
        }
    }

    audioConsumedUpTo[name] = phraseEnd;

    e.getUndoManager().beginNewTransaction ("Cattura audio");
    te::Clip* clip = nullptr;

    if (dest == Destination::session)
    {
        if (auto slot = firstFreeSlot (t))
        {
            auto time = e.tempoSequence.toTime (te::BeatRange (0_bp, placement->length));
            auto wave = te::insertWaveClip (*slot, "Cattura", file, { time }, te::DeleteExistingClips::yes);

            if (wave != nullptr)
            {
                wave->setUsesProxy (false);
                wave->getLoopInfo().setBpm (placement->bpm, wave->getAudioFile().getInfo());
                wave->setAutoTempo (true);
                wave->setLoopRangeBeats ({ 0_bp, placement->length });
                wave->setColour (t.getColour());
                clip = wave.get();
            }
        }
    }
    else
    {
        auto at = placement->clipStart;

        if (! placement->inTime)
        {
            const double bar = arrangement::barLength (e).inBeats();
            const double playhead = e.tempoSequence.toBeats (e.getTransport().getPosition()).inBeats();
            at = te::BeatPosition::fromBeats (std::floor (playhead / bar) * bar);
        }

        clip = arrangement::createClipFromFile (t, at, file);

        if (clip != nullptr)
            clip->setName ("Cattura");
    }

    if (clip == nullptr)
        return juce::Result::fail ("Impossibile creare la clip");

    if (dest == Destination::session && placement->inTime && recorder != nullptr && e.getTransport().isPlaying())
    {
        if (auto context = e.getTransport().getCurrentPlaybackContext())
        {
            if (auto sync = context->getSyncPoint())
            {
                const auto zero = te::MonotonicBeat { sync->monotonicBeat.v - (sync->beat - placement->clipStart) };
                recorder->launchInPhase (*clip, zero);
            }
        }
    }

    if (created != nullptr)
        *created = clip;

    return juce::Result::ok();
}

} // namespace sonora::capture
