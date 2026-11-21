/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

/*  Protocollo tra la DAW e sonora-plugin-scanner.

    La DAW avvia:
        sonora-plugin-scanner --format=<nome formato> --file=<percorso o id> --output=<file xml>

    Lo scanner carica il solo plugin indicato e scrive nel file di output:
        <SONORA-SCAN version="1"> <PLUGIN .../> ... </SONORA-SCAN>
    (ogni figlio e' un juce::PluginDescription serializzato).

    Codici di uscita: 0 = scansione completata (anche con zero plugin trovati),
    2 = argomenti errati, 3 = formato sconosciuto. Un crash o un timeout fanno
    finire il file nella lista di esclusione.
*/

namespace sonora::scan
{
    inline constexpr const char* executableName = "sonora-plugin-scanner";
    inline constexpr const char* rootTag        = "SONORA-SCAN";
    inline constexpr int protocolVersion        = 1;

    inline constexpr int exitOk                 = 0;
    inline constexpr int exitBadArguments       = 2;
    inline constexpr int exitUnknownFormat      = 3;
}
