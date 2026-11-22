/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Warp.h"

namespace te = tracktion;

namespace sonora::warp
{

namespace
{
    const juce::Identifier warpProp ("sonoraWarp");
    const juce::Identifier modeProp ("sonoraWarpMode");

    constexpr double minTempo = 70.0, maxTempo = 180.0;

    te::TimeStretcher::Mode engineMode (Mode m)
    {
        switch (m)
        {
            case Mode::complete:  return te::TimeStretcher::signalsmithDefault;
            case Mode::light:     return te::TimeStretcher::signalsmithCheaper;
            case Mode::repitch:   return te::TimeStretcher::disabled;
        }

        return te::TimeStretcher::defaultMode;
    }

    juce::String key (Mode m)
    {
        switch (m)
        {
            case Mode::complete:  return "completo";
            case Mode::light:     return "leggero";
            case Mode::repitch:   return "ripitch";
        }

        return {};
    }

    double projectTempoAt (const te::AudioClipBase& c)
    {
        return c.edit.tempoSequence.getTempoAt (c.getPosition().getStart()).getBpm();
    }

    /** Allunga o accorcia la clip (e il suo loop) di ratio, tenendo fermo l'inizio. */
    void scaleClip (te::AudioClipBase& c, double ratio)
    {
        if (std::abs (ratio - 1.0) < 1.0e-9)
            return;

        const bool looping = c.isLooping();
        const auto loopBeats = c.getLoopRangeBeats();
        auto pos = c.getPosition();

        if (looping)
        {
            // Il loop si misura sul file: cambia, la lunghezza della clip resta
            if (c.getAutoTempo())
                c.setLoopRangeBeats ({ te::BeatPosition::fromBeats (loopBeats.getStart().inBeats() * ratio),
                                       te::BeatDuration::fromBeats (loopBeats.getLength().inBeats() * ratio) });
            return;
        }

        c.setPosition ({ { pos.getStart(), pos.getStart() + pos.getLength() * ratio }, pos.getOffset() * ratio });
    }

    void forEachAudioClip (te::Edit& edit, const std::function<void (te::AudioClipBase&)>& fn)
    {
        for (auto t : te::getAudioTracks (edit))
        {
            for (auto c : t->getClips())
                if (auto a = dynamic_cast<te::AudioClipBase*> (c))
                    fn (*a);

            for (auto slot : t->getClipSlotList().getClipSlots())
                if (auto a = dynamic_cast<te::AudioClipBase*> (slot->getClip()))
                    fn (*a);
        }
    }

    /** Applica stato e modo al motore. */
    void apply (te::AudioClipBase& c, bool warped, Mode m)
    {
        auto* um = &c.edit.getUndoManager();

        // Stretch in tempo reale: niente file intermedi da ricalcolare a ogni cambio di tempo
        c.setUsesProxy (false);
        c.state.setProperty (warpProp, warped, um);
        c.state.setProperty (modeProp, key (m), um);

        if (! warped)
        {
            c.setAutoTempo (false);
            c.setTimeStretchMode (te::TimeStretcher::disabled);
            c.setSpeedRatio (1.0);
            return;
        }

        if (m == Mode::repitch)
        {
            c.setAutoTempo (false);
            c.setTimeStretchMode (te::TimeStretcher::disabled);
            c.setSpeedRatio (projectTempoAt (c) / getOriginalTempo (c));
            return;
        }

        c.setSpeedRatio (1.0);
        c.setTimeStretchMode (engineMode (m));
        c.setAutoTempo (true);
    }

    /** Le informazioni del file originale (quelle della clip possono riferirsi a un
        file di lavoro non ancora pronto). */
    te::AudioFileInfo fileInfo (const te::AudioClipBase& c)
    {
        return te::AudioFile (c.edit.engine, c.getOriginalFile()).getInfo();
    }

    double clipTimeFor (const te::AudioClipBase& c, double beat)
    {
        return beat * 60.0 / getOriginalTempo (c);
    }
}

//==============================================================================
juce::String name (Mode m)
{
    switch (m)
    {
        case Mode::complete:  return "Completo";
        case Mode::light:     return "Leggero";
        case Mode::repitch:   return "Ripitch";
    }

    return {};
}

juce::Array<Mode> allModes()
{
    return { Mode::complete, Mode::light, Mode::repitch };
}

//==============================================================================
double guessTempo (double seconds, double detectedBpm, int beatsPerBar, double fallback)
{
    auto fold = [] (double bpm)
    {
        while (bpm < minTempo)  bpm *= 2.0;
        while (bpm > maxTempo)  bpm *= 0.5;
        return bpm;
    };

    const bool hasDetected = detectedBpm >= 30.0 && detectedBpm <= 400.0;
    const double detected = hasDetected ? fold (detectedBpm) : 0.0;

    // Un loop corto e' quasi sempre un numero intero di battute (o 1, 2, 4... beat)
    if (seconds > 0.2 && seconds < 40.0)
    {
        // Battute "tonde" (1, 2, 4, 8... come si costruiscono i loop), o 1-2 beat per i frammenti
        juce::Array<double> beats { 1.0, 2.0 };

        for (int bars = 1; bars <= 64; bars *= 2)
            beats.addIfNotAlreadyThere ((double) (bars * juce::jmax (1, beatsPerBar)));

        double best = 0.0, bestScore = 1.0e9;
        const double target = hasDetected ? detected : 120.0;

        for (auto b : beats)
        {
            const double bpm = b * 60.0 / seconds;

            if (bpm < minTempo || bpm > maxTempo)
                continue;

            const double score = std::abs (std::log (bpm / target));

            if (score < bestScore)
            {
                bestScore = score;
                best = bpm;
            }
        }

        // Se il rilevatore e' sicuro e nessuna lunghezza in battute gli torna, ha ragione lui
        if (best > 0.0 && (! hasDetected || bestScore < std::log (1.06)))
            return best;
    }

    if (hasDetected)
        return detected;

    return fallback;
}

double detectTempo (const float* envelope, int numFrames, double framesPerSecond)
{
    if (envelope == nullptr || numFrames < 8 || framesPerSecond <= 0.0)
        return 0.0;

    // Forza degli attacchi: quanto sale l'energia da un fotogramma all'altro
    std::vector<float> onset ((size_t) numFrames, 0.0f);

    for (int i = 1; i < numFrames; ++i)
        onset[(size_t) i] = std::max (0.0f, envelope[i] - envelope[i - 1]);

    float mean = 0.0f;

    for (auto v : onset)
        mean += v;

    mean /= (float) numFrames;

    for (auto& v : onset)
        v = std::max (0.0f, v - mean);

    // Autocorrelazione sui ritardi tra 60 e 200 BPM, con una leggera preferenza per 120
    const int minLag = std::max (1, (int) std::floor (framesPerSecond * 60.0 / 200.0));
    const int maxLag = std::min (numFrames / 2, (int) std::ceil (framesPerSecond * 60.0 / 60.0));
    double best = 0.0, bestScore = 0.0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double sum = 0.0;

        for (int i = lag; i < numFrames; ++i)
            sum += (double) onset[(size_t) i] * (double) onset[(size_t) (i - lag)];

        sum /= (double) (numFrames - lag);

        const double bpm = framesPerSecond * 60.0 / lag;
        const double weight = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.9, 2.0));

        if (sum * weight > bestScore)
        {
            bestScore = sum * weight;
            best = bpm;
        }
    }

    if (bestScore <= 0.0)
        return 0.0;

    return best;
}

double detectTempo (te::Engine& engine, const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file));

    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples < (juce::int64) (reader->sampleRate * 2.0))
        return 0.0;

    // Energia in fotogrammi da ~5 ms, sui primi due minuti al massimo
    const int hop = juce::jmax (64, (int) (reader->sampleRate * 0.005));
    const auto total = std::min (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 120.0));
    const int numFrames = (int) (total / hop);
    std::vector<float> envelope ((size_t) numFrames, 0.0f);

    juce::AudioBuffer<float> buffer ((int) reader->numChannels, hop * 256);

    for (int frame = 0; frame < numFrames; frame += 256)
    {
        const int frames = std::min (256, numFrames - frame);
        reader->read (&buffer, 0, frames * hop, (juce::int64) frame * hop, true, true);

        for (int f = 0; f < frames; ++f)
        {
            double energy = 0.0;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* d = buffer.getReadPointer (ch, f * hop);

                for (int i = 0; i < hop; ++i)
                    energy += (double) d[i] * (double) d[i];
            }

            envelope[(size_t) (frame + f)] = (float) std::log1p (1000.0 * energy / hop);
        }
    }

    return detectTempo (envelope.data(), numFrames, reader->sampleRate / hop);
}

//==============================================================================
bool isWarped (const te::AudioClipBase& c)
{
    if (c.state.hasProperty (warpProp))
        return c.state[warpProp];

    return c.getAutoTempo();
}

Mode getMode (const te::AudioClipBase& c)
{
    const auto k = c.state[modeProp].toString();

    for (auto m : allModes())
        if (key (m) == k)
            return m;

    const auto ts = c.getTimeStretchMode();

    if (ts == te::TimeStretcher::signalsmithCheaper)
        return Mode::light;

    return Mode::complete;
}

double getOriginalTempo (const te::AudioClipBase& c)
{
    const auto info = fileInfo (c);

    if (info.sampleRate > 0.0 && c.getLoopInfo().getNumBeats() > 0.0)
        if (const auto bpm = c.getLoopInfo().getBpm (info); bpm > 1.0)
            return bpm;

    return projectTempoAt (c);
}

void setWarped (te::AudioClipBase& c, bool warped)
{
    if (warped == isWarped (c) && c.state.hasProperty (warpProp))
        return;

    apply (c, warped, getMode (c));
}

void setMode (te::AudioClipBase& c, Mode m)
{
    apply (c, isWarped (c), m);
}

void setOriginalTempo (te::AudioClipBase& c, double bpm)
{
    bpm = juce::jlimit (20.0, 999.0, bpm);
    const auto info = fileInfo (c);
    const double old = getOriginalTempo (c);

    if (info.sampleRate <= 0.0 || std::abs (bpm - old) < 1.0e-6)
        return;

    auto loopInfo = c.getLoopInfo();

    if (loopInfo.getNumBeats() <= 0.0)
        loopInfo.setNumBeats (info.getLengthInSeconds() * old / 60.0);

    loopInfo.setNumBeats (info.getLengthInSeconds() * bpm / 60.0);
    c.setLoopInfo (loopInfo);

    if (! isWarped (c))
        return;

    if (getMode (c) == Mode::repitch)
        c.setSpeedRatio (projectTempoAt (c) / bpm);

    scaleClip (c, bpm / old);
}

void prepareImported (te::AudioClipBase& c)
{
    const auto file = c.getOriginalFile();
    const auto info = fileInfo (c);
    const double seconds = info.getLengthInSeconds();

    if (seconds <= 0.0)
        return;

    auto& sig = c.edit.tempoSequence.getTimeSigAt (c.getPosition().getStart());
    const double project = projectTempoAt (c);
    const double bpm = guessTempo (seconds, detectTempo (c.edit.engine, file), sig.numerator, project);

    auto loopInfo = c.getLoopInfo();
    loopInfo.setNumBeats (seconds * bpm / 60.0);
    c.setLoopInfo (loopInfo);

    // I colpi singoli (meno di un beat) restano come sono
    if (seconds < 60.0 / bpm * 0.95)
    {
        apply (c, false, Mode::complete);
        return;
    }

    apply (c, true, Mode::complete);

    const auto start = c.getPosition().getStart();
    const auto end = c.edit.tempoSequence.toTime (c.edit.tempoSequence.toBeats (start)
                                                  + te::BeatDuration::fromBeats (loopInfo.getNumBeats()));
    c.setPosition ({ { start, end }, {} });
}

int followTempo (te::Edit& edit)
{
    int changed = 0;

    forEachAudioClip (edit, [&changed] (te::AudioClipBase& c)
    {
        if (! isWarped (c) || getMode (c) != Mode::repitch)
            return;

        const double speed = projectTempoAt (c) / getOriginalTempo (c);
        const double old = c.getSpeedRatio();

        if (std::abs (speed - old) < 1.0e-6)
            return;

        c.setSpeedRatio (speed);
        scaleClip (c, old / speed);
        ++changed;
    });

    return changed;
}

std::optional<double> sourceSecondsAt (const te::AudioClipBase& c, te::TimePosition t)
{
    const auto pos = c.getPosition();

    if (t < pos.getStart() || t > pos.getEnd())
        return std::nullopt;

    double local = 0.0;     // secondi "a tempo originale" dall'inizio del file, prima dei marker

    if (c.getAutoTempo())
    {
        auto& ts = c.edit.tempoSequence;
        const double offsetBeats = pos.getOffset().inSeconds() * ts.getBeatsPerSecondAt (pos.getStart());
        double beat = (ts.toBeats (t) - ts.toBeats (pos.getStart())).inBeats() + offsetBeats;

        if (c.isLooping())
        {
            const auto loop = c.getLoopRangeBeats();
            const double len = loop.getLength().inBeats();

            if (len > 0.0)
                beat = loop.getStart().inBeats() + std::fmod (beat, len);
        }

        local = beat * 60.0 / getOriginalTempo (c);
    }
    else
    {
        local = (t - pos.getStart() + pos.getOffset()).inSeconds() * c.getSpeedRatio();

        if (c.isLooping())
        {
            const auto loop = c.getLoopRange();
            const double len = loop.getLength().inSeconds() * c.getSpeedRatio();

            if (len > 0.0)
                local = loop.getStart().inSeconds() * c.getSpeedRatio() + std::fmod (local, len);
        }
    }

    if (c.getWarpTime())
        return c.getWarpTimeManager().warpTimeToSourceTime (te::TimePosition::fromSeconds (local)).inSeconds();

    return local;
}

double beatForSource (const te::AudioClipBase& c, double sourceSeconds)
{
    double warpSeconds = sourceSeconds;

    if (c.getWarpTime())
        warpSeconds = c.getWarpTimeManager().sourceTimeToWarpTime (te::TimePosition::fromSeconds (sourceSeconds)).inSeconds();

    return warpSeconds * getOriginalTempo (c) / 60.0;
}

double sourceForBeat (const te::AudioClipBase& c, double beat)
{
    const double warpSeconds = clipTimeFor (c, beat);

    if (c.getWarpTime())
        return c.getWarpTimeManager().warpTimeToSourceTime (te::TimePosition::fromSeconds (warpSeconds)).inSeconds();

    return warpSeconds;
}

double sourceLength (const te::AudioClipBase& c)
{
    return fileInfo (c).getLengthInSeconds();
}

//==============================================================================
int getTranspose (const te::AudioClipBase& c)
{
    return juce::roundToInt (c.getPitchChange());
}

int getDetune (const te::AudioClipBase& c)
{
    return juce::roundToInt ((c.getPitchChange() - (float) getTranspose (c)) * 100.0f);
}

void setTranspose (te::AudioClipBase& c, int semitones)
{
    c.setPitchChange ((float) juce::jlimit (-48, 48, semitones) + (float) getDetune (c) / 100.0f);
}

void setDetune (te::AudioClipBase& c, int cents)
{
    c.setPitchChange ((float) getTranspose (c) + (float) juce::jlimit (-50, 50, cents) / 100.0f);
}

//==============================================================================
std::vector<Marker> getMarkers (const te::AudioClipBase& c)
{
    std::vector<Marker> result;

    if (! c.getWarpTime())
        return result;

    const double bps = getOriginalTempo (c) / 60.0;

    for (auto m : c.getWarpTimeManager().getMarkers())
        result.push_back ({ m->sourceTime.inSeconds(), m->warpTime.inSeconds() * bps });

    return result;
}

int addMarker (te::AudioClipBase& c, double sourceSeconds)
{
    auto& wtm = c.getWarpTimeManager();
    const auto source = te::TimePosition::fromSeconds (juce::jlimit (0.0, wtm.getSourceLength().inSeconds(), sourceSeconds));

    for (auto m : wtm.getMarkers())
        if (std::abs ((m->sourceTime - source).inSeconds()) < 0.005)
            return -1;

    const auto warpTime = c.getWarpTime() ? wtm.sourceTimeToWarpTime (source) : source;
    c.setWarpTime (true);
    return wtm.insertMarker ({ source, warpTime });
}

void moveMarker (te::AudioClipBase& c, int index, double beat)
{
    auto& wtm = c.getWarpTimeManager();
    const auto& markers = wtm.getMarkers();

    if (! juce::isPositiveAndBelow (index, markers.size()))
        return;

    // Tra i vicini, con un minimo di spazio: l'audio non puo' andare all'indietro
    constexpr double minGap = 0.01;
    double lo = -1.0e9, hi = 1.0e9;

    if (index > 0)                    lo = markers[index - 1]->warpTime.inSeconds() + minGap;
    if (index < markers.size() - 1)   hi = markers[index + 1]->warpTime.inSeconds() - minGap;

    if (lo > hi)
        return;

    wtm.moveMarker (index, te::TimePosition::fromSeconds (juce::jlimit (lo, hi, clipTimeFor (c, beat))));
}

void removeMarker (te::AudioClipBase& c, int index)
{
    auto& wtm = c.getWarpTimeManager();

    if (index > 0 && index < wtm.getMarkers().size() - 1)
        wtm.removeMarker (index);
}

void resetMarkers (te::AudioClipBase& c)
{
    auto& wtm = c.getWarpTimeManager();
    const auto len = te::toPosition (wtm.getSourceLength());
    wtm.removeAllMarkers();
    wtm.insertMarker ({});
    wtm.insertMarker ({ len, len });
    wtm.setWarpEndMarkerTime (len);
    c.setWarpTime (false);
}

} // namespace sonora::warp
