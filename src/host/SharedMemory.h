/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_core/juce_core.h>

namespace sonora::sandbox
{

/** Memoria condivisa tra due processi: un file mappato in memoria (su Linux in
    /dev/shm, quindi solo RAM). Chi lo crea lo cancella alla fine. */
class SharedRegion
{
public:
    /** Crea un file nuovo di size byte, azzerato. */
    static std::unique_ptr<SharedRegion> create (const juce::String& name, size_t size);

    /** Apre un file creato dall'altro processo. */
    static std::unique_ptr<SharedRegion> open (const juce::String& path, size_t size);

    ~SharedRegion();

    void* getData() const noexcept          { return mapped != nullptr ? mapped->getData() : nullptr; }
    juce::String getPath() const            { return file.getFullPathName(); }

private:
    SharedRegion (juce::File, bool owner);

    juce::File file;
    bool owner;
    std::unique_ptr<juce::MemoryMappedFile> mapped;
};

//==============================================================================
/** Semaforo con nome, visibile a un altro processo (POSIX o Windows). */
class NamedSemaphore
{
public:
    static std::unique_ptr<NamedSemaphore> create (const juce::String& name);
    static std::unique_ptr<NamedSemaphore> open (const juce::String& name);
    ~NamedSemaphore();

    void signal();

    /** Aspetta il segnale; false se scade il tempo. */
    bool wait (int timeoutMs);

    /** Toglie i segnali rimasti (dopo una risposta arrivata in ritardo). */
    void drain();

private:
    NamedSemaphore (juce::String name, void* handle, bool owner);

    juce::String name;
    void* handle;
    bool owner;
};

/** Un nome nuovo e unico per memoria e semafori di un plugin. */
juce::String createUniqueName();

} // namespace sonora::sandbox
