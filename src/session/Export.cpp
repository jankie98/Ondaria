/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Export.h"
#include "Arrangement.h"

namespace te = tracktion;

namespace sonora::exporting
{

namespace
{
    te::RenderFormat toRenderFormat (Format f)
    {
        switch (f)
        {
            case Format::wav:   return te::RenderFormat::wav;
            case Format::flac:  return te::RenderFormat::flac;
            case Format::mp3:   return te::RenderFormat::mp3;
            case Format::ogg:   return te::RenderFormat::ogg;
        }

        return te::RenderFormat::wav;
    }

    juce::AudioFormat* audioFormatFor (te::Engine& engine, Format f)
    {
        auto& afm = engine.getAudioFileFormatManager();

        switch (f)
        {
            case Format::wav:   return afm.getWavFormat();
            case Format::flac:  return afm.getFlacFormat();
            case Format::ogg:   return afm.getOggFormat();
            case Format::mp3:
                te::LAMEManager::registerAudioFormat (afm);
                return afm.getLameFormat();
        }

        return nullptr;
    }

    /** Indice della qualita' "N Kb/s CBR" fra quelle del codificatore MP3. */
    int mp3QualityIndex (juce::AudioFormat& format, int kbps)
    {
        auto options = format.getQualityOptions();
        int best = -1, bestDistance = std::numeric_limits<int>::max();

        for (int i = 0; i < options.size(); ++i)
        {
            if (! options[i].contains ("CBR"))
                continue;

            const int distance = std::abs (options[i].getIntValue() - kbps);

            if (distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }

        return juce::jmax (0, best);
    }

    juce::String projectName (te::Edit& edit)
    {
        auto name = te::EditFileOperations (edit).getEditFile().getFileNameWithoutExtension();
        return name.isNotEmpty() ? name : juce::String ("Brano");
    }

    te::RenderSpecification baseSpec (te::Edit& edit, const Settings& s, juce::AudioFormat& format)
    {
        te::RenderSpecification spec;
        spec.format = toRenderFormat (s.format);
        spec.sampleRate = s.sampleRate;
        spec.channelLayout = "stereo";
        spec.time = timeRangeFor (edit, s.range);
        spec.includeTails = s.includeTails;
        spec.normalise = s.normalise;
        spec.normaliseToLevelDb = -0.3f;

        const auto depths = bitDepthsFor (s.format);
        spec.bitDepth = depths.isEmpty() ? 16 : (depths.contains (s.bitDepth) ? s.bitDepth : depths.getLast());
        spec.dither = spec.bitDepth < 24 && s.format != Format::mp3 && s.format != Format::ogg;

        if (s.format == Format::mp3)
            spec.quality = mp3QualityIndex (format, s.mp3Kbps);
        else if (s.format == Format::ogg)
            spec.quality = juce::jmax (0, format.getQualityOptions().size() - 2);   // alta qualita'

        return spec;
    }
}

juce::String extensionFor (Format f)
{
    switch (f)
    {
        case Format::wav:   return ".wav";
        case Format::flac:  return ".flac";
        case Format::mp3:   return ".mp3";
        case Format::ogg:   return ".ogg";
    }

    return ".wav";
}

juce::String nameFor (Format f)
{
    switch (f)
    {
        case Format::wav:   return "WAV";
        case Format::flac:  return "FLAC";
        case Format::mp3:   return "MP3";
        case Format::ogg:   return "Ogg Vorbis";
    }

    return {};
}

bool isAvailable (te::Engine& engine, Format f)
{
    return audioFormatFor (engine, f) != nullptr;
}

juce::Array<int> bitDepthsFor (Format f)
{
    switch (f)
    {
        case Format::wav:   return { 16, 24, 32 };
        case Format::flac:  return { 16, 24 };
        case Format::mp3:
        case Format::ogg:   break;
    }

    return {};
}

std::optional<te::TimeRange> timeRangeFor (te::Edit& edit, Range r)
{
    if (r == Range::loop)
    {
        const auto loop = edit.getTransport().getLoopRange();

        if (loop.getLength() > te::TimeDuration())
            return loop;

        return std::nullopt;
    }

    const auto end = arrangement::getEndOfContent (edit, 0);

    if (end <= te::BeatPosition())
        return std::nullopt;

    return te::TimeRange (te::TimePosition(), edit.tempoSequence.toTime (end));
}

juce::File defaultDestination (te::Edit& edit, const Settings& s)
{
    auto editFile = te::EditFileOperations (edit).getEditFile();
    auto dir = editFile.existsAsFile() ? editFile.getParentDirectory()
                                       : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    const auto name = juce::File::createLegalFileName (projectName (edit));

    if (s.stems)
        return dir.getChildFile (name + " - stem");

    return dir.getChildFile (name + extensionFor (s.format));
}

juce::Result planJobs (te::Edit& edit, const Settings& s, std::vector<te::PlannedRenderJob>& jobs)
{
    jobs.clear();
    auto format = audioFormatFor (edit.engine, s.format);

    if (format == nullptr)
        return juce::Result::fail (s.format == Format::mp3
                                     ? "Per l'MP3 serve la libreria LAME (su Arch: pacman -S lame)"
                                     : "Formato non disponibile: " + nameFor (s.format));

    if (! timeRangeFor (edit, s.range))
        return juce::Result::fail (juce::String::fromUTF8 (s.range == Range::loop ? "Il loop non \xc3\xa8 impostato"
                                                                                  : "Non c'\xc3\xa8 niente da esportare: l'arrangement \xc3\xa8 vuoto"));

    if (s.destination == juce::File())
        return juce::Result::fail ("Scegli dove salvare");

    std::vector<std::pair<te::RenderSpecification, juce::String>> specs;

    if (! s.stems)
    {
        auto spec = baseSpec (edit, s, *format);
        spec.destination = s.destination.withFileExtension (extensionFor (s.format));
        spec.metadata = te::translateMetadataForFormat (te::createTagMetadata (edit), spec.format);
        specs.push_back ({ std::move (spec), "Mix" });
    }
    else
    {
        if (! s.destination.isDirectory() && ! s.destination.createDirectory())
            return juce::Result::fail ("Non riesco a creare la cartella " + s.destination.getFullPathName());

        int index = 0;

        for (auto t : te::getAudioTracks (edit))
        {
            ++index;

            // Una traccia senza clip nell'arrangement darebbe solo silenzio
            if (t->getClips().isEmpty())
                continue;

            auto spec = baseSpec (edit, s, *format);
            spec.tracks.add (t->itemID);
            spec.includeSourceTracks = true;
            spec.includeTails = s.includeTails;

            // Numerate come nel progetto, cosi' restano in ordine nella cartella
            const auto base = juce::String (index).paddedLeft ('0', 2) + " " + juce::File::createLegalFileName (t->getName());
            spec.destination = s.destination.getChildFile (base + extensionFor (s.format));
            specs.push_back ({ std::move (spec), t->getName() });
        }

        if (specs.empty())
            return juce::Result::fail ("Nessuna traccia ha clip nell'arrangement");
    }

    for (auto& [spec, name] : specs)
    {
        if (auto r = te::validateRenderSpecification (edit, spec); r.failed())
            return r;

        auto job = te::createRenderJob (edit, spec);

        if (! job)
            return juce::Result::fail ("Non riesco a preparare l'export di " + name);

        job->name = name;
        jobs.push_back (std::move (*job));
    }

    return juce::Result::ok();
}

//==============================================================================
Exporter::~Exporter()
{
    onFinished = nullptr;
    cancel();
}

juce::Result Exporter::start (te::Edit& edit, const Settings& s)
{
    if (isRunning())
        return juce::Result::fail (juce::String::fromUTF8 ("C'\xc3\xa8 gi\xc3\xa0 un export in corso"));

    std::vector<te::PlannedRenderJob> jobs;

    if (auto r = planJobs (edit, s, jobs); r.failed())
        return r;

    queue = std::make_unique<te::RenderQueue>();
    queue->addJobs (std::move (jobs));
    queue->onJobStarted = [this] (te::RenderQueue::Job& j) { current = j.getName(); };
    queue->onFinished = [this]
    {
        juce::Array<juce::File> files;
        juce::StringArray errors;
        bool cancelled = false;

        for (auto& j : queue->getJobs())
        {
            switch (j->getState())
            {
                case te::RenderQueue::Job::State::completed:  files.add (j->getFile()); break;
                case te::RenderQueue::Job::State::failed:     errors.add (j->getName() + ": " + j->getError()); break;
                case te::RenderQueue::Job::State::cancelled:  cancelled = true; break;
                case te::RenderQueue::Job::State::pending:
                case te::RenderQueue::Job::State::active:     break;
            }
        }

        current = {};
        const auto result = ! errors.isEmpty() ? juce::Result::fail (errors.joinIntoString ("\n"))
                          : cancelled          ? juce::Result::fail ("Export annullato")
                                               : juce::Result::ok();

        if (onFinished)
            onFinished (result, files);
    };

    queue->start();
    return juce::Result::ok();
}

bool Exporter::isRunning() const
{
    return queue != nullptr && queue->hasStarted() && ! queue->hasFinished();
}

float Exporter::getProgress() const
{
    return queue != nullptr ? queue->getTotalProgress() : 0.0f;
}

juce::String Exporter::getCurrentJobName() const
{
    return current;
}

void Exporter::cancel()
{
    if (queue != nullptr)
        queue->cancelAll();
}

} // namespace sonora::exporting
