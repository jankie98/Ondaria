/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <cstdint>

/*  Protocollo tra la DAW e un plugin isolato (ondaria-plugin-scanner --host).

    Ogni plugin isolato gira in un processo suo. Due canali:

    - Controllo: messaggi ChildProcessCoordinator/Worker di JUCE (una pipe), con un
      ValueTree per messaggio. Carica, prepara, stato, finestra. Le richieste con
      "req" aspettano una risposta con lo stesso "req".

    - Audio: un blocco di memoria condivisa (SharedBlock, un file mappato) e due
      semafori con nome. Per ogni blocco la DAW scrive ingressi, MIDI e parametri,
      alza "richiesta" e aspetta "risposta"; il processo del plugin elabora e
      scrive le uscite. Nessuna allocazione e nessun messaggio sul percorso audio.

    Se il processo muore o non risponde in tempo, la DAW manda silenzio al posto
    del plugin e continua.
*/
namespace sonora::sandbox::protocol
{
    inline constexpr const char* commandLineID   = "ondaria-plugin-host";
    inline constexpr const char* hostArgument     = "--host";

    inline constexpr int maxChannels     = 8;
    inline constexpr int maxBlockSize    = 8192;
    inline constexpr int maxMidiEvents   = 1024;
    inline constexpr int maxMidiBytes    = 16;
    inline constexpr int maxParamChanges = 512;
    inline constexpr int maxTextLength   = 32;

    struct MidiEvent
    {
        int32_t sampleOffset;
        int32_t size;
        uint8_t data[maxMidiBytes];
    };

    struct ParamChange
    {
        int32_t index;
        float value;
        char text[maxTextLength];   ///< testo del valore (solo dal plugin verso la DAW)
    };

    /** Il blocco condiviso. Lo scrive un lato alla volta: i semafori fanno da barriera. */
    struct SharedBlock
    {
        uint32_t magic;
        uint32_t requestSequence;   ///< scritto dalla DAW
        uint32_t replySequence;     ///< scritto dal plugin quando ha finito quel blocco

        int32_t numSamples;
        int32_t numInputChannels, numOutputChannels;

        // Trasporto, per i plugin che si sincronizzano col tempo
        double bpm, ppqPosition, ppqLoopStart, ppqLoopEnd, barStartPpq;
        int64_t timeInSamples;
        int32_t timeSigNumerator, timeSigDenominator;
        int32_t isPlaying, isRecording, isLooping, hasPosition;

        int32_t numMidiIn;
        MidiEvent midiIn[maxMidiEvents];
        int32_t numMidiOut;
        MidiEvent midiOut[maxMidiEvents];

        int32_t numParamChanges;                 ///< dalla DAW: valori da impostare
        ParamChange paramChanges[maxParamChanges];
        int32_t numParamUpdates;                 ///< dal plugin: valori cambiati (finestra, automazioni interne)
        ParamChange paramUpdates[maxParamChanges];

        float audio[maxChannels][maxBlockSize];  ///< ingresso, poi uscita sugli stessi canali
    };

    inline constexpr uint32_t magicValue = 0x4f4e4441; // "ONDA"

    // Tipi dei messaggi di controllo (tipo del ValueTree)
    inline constexpr const char* load        = "LOAD";        // desc, sampleRate, blockSize, memory, request, reply
    inline constexpr const char* loaded      = "LOADED";      // info sul plugin e parametri
    inline constexpr const char* failed      = "FAILED";      // error
    inline constexpr const char* prepare     = "PREPARE";     // sampleRate, blockSize
    inline constexpr const char* prepared    = "PREPARED";    // latency
    inline constexpr const char* getState    = "GET_STATE";
    inline constexpr const char* state       = "STATE";       // data
    inline constexpr const char* setState    = "SET_STATE";   // data
    inline constexpr const char* done        = "DONE";
    inline constexpr const char* showEditor  = "SHOW_EDITOR";
    inline constexpr const char* hideEditor  = "HIDE_EDITOR";
    inline constexpr const char* latency     = "LATENCY";     // latency (il plugin l'ha cambiata)
    inline constexpr const char* texts       = "TEXTS";       // testi dei valori (si calcolano sul thread dei messaggi)
}
