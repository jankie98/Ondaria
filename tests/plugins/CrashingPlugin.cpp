/*
    Sonora - plugin CLAP di prova che va in crash appena viene caricato,
    per verificare che la scansione isolata protegga la DAW.
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <clap/clap.h>
#include <cstdlib>

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry
{
    CLAP_VERSION_INIT,
    [] (const char*) -> bool { std::abort(); },
    [] () {},
    [] (const char*) -> const void* { return nullptr; }
};
