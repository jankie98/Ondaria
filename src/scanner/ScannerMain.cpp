/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later

    sonora-plugin-scanner: carica un solo file di plugin e ne scrive la
    descrizione. Gira in un processo separato, cosi' un plugin che va in
    crash durante la scansione non porta giu' la DAW.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "host/PluginFormats.h"
#include "host/ScanProtocol.h"

#include <iostream>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <stdlib.h>
#endif

namespace
{
    juce::String getArgument (const juce::ArgumentList& args, const juce::String& name)
    {
        return args.containsOption (name) ? args.getValueForOption (name) : juce::String();
    }
}

int main (int argc, char* argv[])
{
   #if JUCE_WINDOWS
    // Nessuna finestra di errore di Windows se un plugin va in crash:
    // il processo deve solo terminare, cosi' la DAW se ne accorge subito.
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior (0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
   #endif

    juce::ArgumentList args (argc, argv);

    const auto formatName = getArgument (args, "--format");
    const auto fileOrIdentifier = getArgument (args, "--file");
    const juce::File output (getArgument (args, "--output"));

    // Alcuni plugin si aspettano un message thread e un desktop inizializzati.
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (args.containsOption ("--list-formats"))
    {
        std::cout << sonora::getSupportedFormatNames().joinIntoString (", ") << std::endl;
        return sonora::scan::exitOk;
    }

    if (formatName.isEmpty() || fileOrIdentifier.isEmpty() || output == juce::File())
    {
        std::cerr << "Uso: sonora-plugin-scanner --format=<formato> --file=<plugin> --output=<file.xml>" << std::endl;
        return sonora::scan::exitBadArguments;
    }

    juce::AudioPluginFormatManager manager;
    sonora::addAllPluginFormats (manager);

    juce::AudioPluginFormat* format = nullptr;

    for (auto* f : manager.getFormats())
        if (f->getName() == formatName)
            format = f;

    if (format == nullptr)
    {
        std::cerr << "Formato sconosciuto: " << formatName << std::endl;
        return sonora::scan::exitUnknownFormat;
    }

    juce::OwnedArray<juce::PluginDescription> found;
    format->findAllTypesForFile (found, fileOrIdentifier);

    juce::XmlElement root (sonora::scan::rootTag);
    root.setAttribute ("version", sonora::scan::protocolVersion);

    for (auto* desc : found)
        root.addChildElement (desc->createXml().release());

    if (! root.writeTo (output))
        return sonora::scan::exitBadArguments;

    // Uscita immediata: alcuni plugin vanno in crash nei distruttori statici,
    // e a questo punto il risultato e' gia' salvato.
    std::fflush (stdout);
    std::_Exit (sonora::scan::exitOk);
}
