/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "SharedMemory.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <semaphore.h>
 #include <cerrno>
 #include <ctime>
#endif

namespace sonora::sandbox
{

namespace
{
    juce::File sharedFolder()
    {
       #if JUCE_LINUX
        if (juce::File ("/dev/shm").isDirectory())
            return juce::File ("/dev/shm");
       #endif

        return juce::File::getSpecialLocation (juce::File::tempDirectory);
    }
}

juce::String createUniqueName()
{
    return "ondaria-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()).paddedLeft ('0', 16);
}

//==============================================================================
SharedRegion::SharedRegion (juce::File f, bool isOwner) : file (std::move (f)), owner (isOwner) {}

std::unique_ptr<SharedRegion> SharedRegion::create (const juce::String& name, size_t size)
{
    auto f = sharedFolder().getChildFile (name + ".mem");
    f.deleteFile();

    {
        juce::FileOutputStream out (f);

        if (! out.openedOk())
            return {};

        juce::HeapBlock<char> zeros (64 * 1024, true);

        for (size_t written = 0; written < size;)
        {
            const auto chunk = juce::jmin ((size_t) 64 * 1024, size - written);

            if (! out.write (zeros, chunk))
                return {};

            written += chunk;
        }
    }

    std::unique_ptr<SharedRegion> r (new SharedRegion (f, true));
    r->mapped = std::make_unique<juce::MemoryMappedFile> (f, juce::MemoryMappedFile::readWrite, false);

    if (r->mapped->getData() == nullptr || r->mapped->getSize() < size)
        return {};

    return r;
}

std::unique_ptr<SharedRegion> SharedRegion::open (const juce::String& path, size_t size)
{
    juce::File f (path);

    if (! f.existsAsFile() || (size_t) f.getSize() < size)
        return {};

    std::unique_ptr<SharedRegion> r (new SharedRegion (f, false));
    r->mapped = std::make_unique<juce::MemoryMappedFile> (f, juce::MemoryMappedFile::readWrite, false);

    if (r->mapped->getData() == nullptr || r->mapped->getSize() < size)
        return {};

    return r;
}

SharedRegion::~SharedRegion()
{
    mapped.reset();

    if (owner)
        file.deleteFile();
}

//==============================================================================
NamedSemaphore::NamedSemaphore (juce::String n, void* h, bool isOwner) : name (std::move (n)), handle (h), owner (isOwner) {}

#if JUCE_WINDOWS

static juce::String windowsName (const juce::String& n)     { return "Local\\" + n; }

std::unique_ptr<NamedSemaphore> NamedSemaphore::create (const juce::String& n)
{
    if (auto h = CreateSemaphoreW (nullptr, 0, 0x7fffffff, windowsName (n).toWideCharPointer()))
        return std::unique_ptr<NamedSemaphore> (new NamedSemaphore (n, h, true));

    return {};
}

std::unique_ptr<NamedSemaphore> NamedSemaphore::open (const juce::String& n)
{
    if (auto h = OpenSemaphoreW (SEMAPHORE_ALL_ACCESS, FALSE, windowsName (n).toWideCharPointer()))
        return std::unique_ptr<NamedSemaphore> (new NamedSemaphore (n, h, false));

    return {};
}

NamedSemaphore::~NamedSemaphore()                 { CloseHandle ((HANDLE) handle); }
void NamedSemaphore::signal()                     { ReleaseSemaphore ((HANDLE) handle, 1, nullptr); }
bool NamedSemaphore::wait (int timeoutMs)         { return WaitForSingleObject ((HANDLE) handle, (DWORD) juce::jmax (0, timeoutMs)) == WAIT_OBJECT_0; }
void NamedSemaphore::drain()                      { while (WaitForSingleObject ((HANDLE) handle, 0) == WAIT_OBJECT_0) {} }

#else

static std::string posixName (const juce::String& n)    { return ("/" + n).toStdString(); }

std::unique_ptr<NamedSemaphore> NamedSemaphore::create (const juce::String& n)
{
    sem_unlink (posixName (n).c_str());
    auto s = sem_open (posixName (n).c_str(), O_CREAT | O_EXCL, 0600, 0);

    if (s == SEM_FAILED)
        return {};

    return std::unique_ptr<NamedSemaphore> (new NamedSemaphore (n, s, true));
}

std::unique_ptr<NamedSemaphore> NamedSemaphore::open (const juce::String& n)
{
    auto s = sem_open (posixName (n).c_str(), 0);

    if (s == SEM_FAILED)
        return {};

    return std::unique_ptr<NamedSemaphore> (new NamedSemaphore (n, s, false));
}

NamedSemaphore::~NamedSemaphore()
{
    sem_close ((sem_t*) handle);

    if (owner)
        sem_unlink (posixName (name).c_str());
}

void NamedSemaphore::signal()
{
    sem_post ((sem_t*) handle);
}

bool NamedSemaphore::wait (int timeoutMs)
{
    timespec deadline;
    clock_gettime (CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeoutMs / 1000;
    deadline.tv_nsec += (long) (timeoutMs % 1000) * 1000000L;

    if (deadline.tv_nsec >= 1000000000L)
    {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    for (;;)
    {
        if (sem_timedwait ((sem_t*) handle, &deadline) == 0)
            return true;

        if (errno != EINTR)
            return false;
    }
}

void NamedSemaphore::drain()
{
    while (sem_trywait ((sem_t*) handle) == 0) {}
}

#endif

} // namespace sonora::sandbox
