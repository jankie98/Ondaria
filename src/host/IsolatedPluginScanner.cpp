/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "IsolatedPluginScanner.h"
#include "ScanProtocol.h"

namespace sonora
{

IsolatedPluginScanner::IsolatedPluginScanner (juce::File exe, int timeout)
    : scanner (std::move (exe)), timeoutMs (timeout)
{
}

juce::File IsolatedPluginScanner::findScannerExecutable()
{
    auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();

   #if JUCE_WINDOWS
    return dir.getChildFile (juce::String (scan::executableName) + ".exe");
   #else
    return dir.getChildFile (scan::executableName);
   #endif
}

bool IsolatedPluginScanner::findPluginTypesFor (juce::AudioPluginFormat& format,
                                                juce::OwnedArray<juce::PluginDescription>& result,
                                                const juce::String& fileOrIdentifier)
{
    // I formati che non caricano codice si possono scansionare qui senza rischi.
    if (format.isTrivialToScan())
    {
        format.findAllTypesForFile (result, fileOrIdentifier);
        return true;
    }

    switch (scanFile (format.getName(), fileOrIdentifier, result, [this] { return shouldExit(); }))
    {
        case Outcome::found:
        case Outcome::cancelled:
            return true;

        case Outcome::scannerMissing:
            // Senza scanner non possiamo isolare: meglio non caricare nulla
            // che rischiare un crash della DAW.
            DBG ("ondaria-plugin-scanner non trovato: " << scanner.getFullPathName());
            return true;

        case Outcome::crashed:
        case Outcome::timedOut:
            return false;
    }

    return false;
}

IsolatedPluginScanner::Outcome IsolatedPluginScanner::scanFile (const juce::String& formatName,
                                                                const juce::String& fileOrIdentifier,
                                                                juce::OwnedArray<juce::PluginDescription>& result,
                                                                std::function<bool()> shouldCancel) const
{
    if (! scanner.existsAsFile())
        return Outcome::scannerMissing;

    juce::TemporaryFile output (".xml");

    juce::StringArray args { scanner.getFullPathName(),
                             "--format=" + formatName,
                             "--file=" + fileOrIdentifier,
                             "--output=" + output.getFile().getFullPathName() };

    juce::ChildProcess process;

    // Niente pipe per stdout/stderr: i plugin che scrivono molto bloccherebbero lo scanner.
    if (! process.start (args, 0))
        return Outcome::scannerMissing;

    const auto start = juce::Time::getMillisecondCounter();

    while (process.isRunning())
    {
        if (shouldCancel && shouldCancel())
        {
            process.kill();
            return Outcome::cancelled;
        }

        if (juce::Time::getMillisecondCounter() - start > (juce::uint32) timeoutMs)
        {
            process.kill();
            return Outcome::timedOut;
        }

        juce::Thread::sleep (5);
    }

    if (process.getExitCode() != scan::exitOk)
        return Outcome::crashed;

    auto xml = juce::parseXML (output.getFile());

    if (xml == nullptr || ! xml->hasTagName (scan::rootTag))
        return Outcome::crashed;

    for (auto* child : xml->getChildIterator())
    {
        juce::PluginDescription desc;

        if (desc.loadFromXml (*child))
            result.add (new juce::PluginDescription (desc));
    }

    return Outcome::found;
}

} // namespace sonora
