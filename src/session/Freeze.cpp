/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Freeze.h"

namespace te = tracktion;

namespace sonora::freeze
{

namespace
{
    bool isMixerPlugin (te::Plugin* p)
    {
        return dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr
            || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr
            || dynamic_cast<te::FreezePointPlugin*> (p) != nullptr;
    }

    juce::File flattenFolder (te::Edit& e)
    {
        auto editFile = te::EditFileOperations (e).getEditFile();
        auto folder = editFile != juce::File() && editFile.getParentDirectory().isDirectory()
                        ? editFile.getParentDirectory().getChildFile ("Appiattite")
                        : e.engine.getTemporaryFileManager().getTempDirectory().getChildFile ("Appiattite");
        folder.createDirectory();
        return folder;
    }

    /** Copia il file del freeze (formato interno del motore) in un WAV a 32 bit float. */
    juce::Result copyToWav (te::Engine& engine, const juce::File& source, const juce::File& dest)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (engine.getAudioFileFormatManager().readFormatManager.createReaderFor (source));

        if (reader == nullptr)
            return juce::Result::fail ("Non riesco a leggere il file del freeze");

        te::AudioFileWriter writer (te::AudioFile (engine, dest), engine.getAudioFileFormatManager().getWavFormat(),
                                    (int) reader->numChannels, reader->sampleRate, 32, {}, 0);

        if (! writer.isOpen())
            return juce::Result::fail ("Non riesco a scrivere " + dest.getFullPathName());

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, 8192);

        for (juce::int64 pos = 0; pos < reader->lengthInSamples;)
        {
            const int num = (int) std::min<juce::int64> (buffer.getNumSamples(), reader->lengthInSamples - pos);
            reader->read (&buffer, 0, num, pos, true, true);

            if (! writer.appendBuffer (buffer, num))
                return juce::Result::fail ("Scrittura interrotta: " + dest.getFullPathName());

            pos += num;
        }

        writer.closeForWriting();
        return juce::Result::ok();
    }
}

bool isFrozen (const te::AudioTrack& t)
{
    return t.isFrozen (te::Track::individualFreeze);
}

bool canFreeze (te::AudioTrack& t, juce::String* why)
{
    auto fail = [why] (const juce::String& reason)
    {
        if (why != nullptr)
            *why = reason;

        return false;
    };

    if (isFrozen (t))
        return fail (juce::String::fromUTF8 ("La traccia \xc3\xa8 gi\xc3\xa0 congelata"));

    if (t.getClips().isEmpty())
        return fail (juce::String::fromUTF8 ("Non c'\xc3\xa8 niente da congelare: la traccia non ha clip nell'arrangement"));

    if (t.getOutput().getDestinationTrack() != nullptr)
        return fail ("La traccia manda il suono a un'altra traccia: congela quella");

    return true;
}

juce::Result freeze (te::AudioTrack& t)
{
    juce::String why;

    if (! canFreeze (t, &why))
        return juce::Result::fail (why);

    // Il freeze suona la linea del tempo, e il render vuole il motore fermo
    t.playSlotClips = false;
    auto& transport = t.edit.getTransport();
    transport.stop (false, true);

    // Senza grafo di riproduzione durante il render: altrimenti il motore apre il file
    // del freeze mentre ancora non esiste, e ne tiene in cache i dati vuoti (suonerebbe muto)
    transport.freePlaybackContext();
    auto result = t.setFrozen (true, te::Track::individualFreeze);

    auto& files = t.edit.engine.getAudioFileManager();
    const te::AudioFile freezeAudio (t.edit.engine, te::TemporaryFileManager::getFreezeFileForTrack (t));
    files.validateFile (freezeAudio, true);   // info aggiornate, e via un eventuale "file mancante" in cache
    files.releaseFile (freezeAudio);
    transport.ensureContextAllocated();

    if (result.failed())
        return result;

    if (! isFrozen (t))
        return juce::Result::fail (juce::String::fromUTF8 ("Il render del freeze non \xc3\xa8 riuscito"));


    return juce::Result::ok();
}

void unfreeze (te::AudioTrack& t)
{
    if (isFrozen (t))
        t.unfreeze (te::Track::individualFreeze);
}

juce::Result flatten (te::AudioTrack& t, te::Clip** flatClip)
{
    if (! isFrozen (t))
        if (auto r = freeze (t); r.failed())
            return r;

    auto& e = t.edit;
    const auto freezeFile = te::TemporaryFileManager::getFreezeFileForTrack (t);
    auto dest = flattenFolder (e).getNonexistentChildFile (juce::File::createLegalFileName (t.getName()), ".wav", false);

    if (auto r = copyToWav (e.engine, freezeFile, dest); r.failed())
        return r;

    const te::AudioFile audio (e.engine, dest);

    if (! audio.isValid())
        return juce::Result::fail (juce::String::fromUTF8 ("Il file appiattito non \xc3\xa8 valido"));

    unfreeze (t);
    t.removeFreezePoint();

    // Via clip e device prima del fader (il fader, il pan e il meter restano)
    // Copie delle liste: togliendo gli elementi quelle della traccia cambiano sotto i piedi
    const juce::Array<te::Clip*> clips (t.getClips());

    for (auto c : clips)
        c->removeFromParent();

    const auto plugins = t.pluginList.getPlugins();

    for (auto p : plugins)
        if (! isMixerPlugin (p))
            p->deleteFromParent();

    auto clip = te::insertWaveClip (t, t.getName(), dest,
                                    { { te::TimePosition(), te::TimeDuration::fromSeconds (audio.getLength()) } },
                                    te::DeleteExistingClips::no);

    if (clip == nullptr)
        return juce::Result::fail ("Non riesco a creare la clip appiattita");

    clip->setColour (t.getColour());

    if (flatClip != nullptr)
        *flatClip = clip.get();

    return juce::Result::ok();
}

} // namespace sonora::freeze
