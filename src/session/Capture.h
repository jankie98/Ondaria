/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace sonora::recording { class Recorder; }

/** Cattura retroattiva: Sonora ascolta sempre gli ingressi (MIDI e audio), anche
    senza registrare. Con un comando quello che hai appena suonato diventa una clip,
    allineata alle battute se il trasporto andava, altrimenti come un loop nuovo.
*/
namespace sonora::capture
{

/** Dove mettere le clip catturate. */
enum class Destination { session, arrangement };

/** Ascolta tutti gli ingressi del motore e tiene gli ultimi secondi in memoria.
    Uno per motore: resta in ascolto anche quando si cambia progetto.
*/
class Capturer final : private juce::AudioIODeviceCallback,
                       private tracktion::PhysicalMidiInputDevice::Listener,
                       private juce::Timer
{
public:
    explicit Capturer (tracktion::Engine&, double audioSeconds = 60.0);
    ~Capturer() override;

    /** Il progetto di cui seguire il trasporto (per allineare quello che si suona). */
    void setEdit (tracktion::Edit*);

    /** Un messaggio suonato adesso (per esempio dalla tastiera su schermo). */
    void addLiveMidi (const juce::MidiMessage&);

    /** Un messaggio arrivato a una posizione data, in campioni del dispositivo
        (serve ai test, che fanno girare il motore piu' veloce del tempo reale). */
    void addMidiAt (const juce::MidiMessage&, int64_t devicePosition);

    /** Campioni elaborati dal dispositivo audio da quando ascoltiamo. */
    int64_t getDevicePosition() const       { return devicePosition.load(); }

    /** C'e' qualcosa di nuovo da catturare per la traccia (in base al suo ingresso)? */
    bool hasSomethingFor (const tracktion::AudioTrack&) const;

    /** Cattura per tutte le tracce armate. clipsCreated riceve le clip nuove. */
    juce::Result captureArmedTracks (recording::Recorder&, Destination,
                                     juce::Array<tracktion::Clip*>* clipsCreated = nullptr);

    /** Cattura per una traccia, dal suo ingresso. */
    juce::Result captureTrack (tracktion::AudioTrack&, Destination, recording::Recorder*,
                               tracktion::Clip** clipCreated = nullptr);

    /** Posizione nel progetto (in campioni) meno posizione del dispositivo, mentre il trasporto va. */
    std::optional<int64_t> getTransportOffset() const;

    /** Aggiorna subito l'allineamento con il trasporto (di solito lo fa il timer). */
    void updateTransportMapping();

private:
    struct MidiEvent
    {
        int64_t position;
        juce::MidiMessage message;
    };

    /** Tratto in cui il trasporto andava: posizione nel progetto = campione + offset. */
    struct Segment
    {
        int64_t start = 0, end = 0, offset = 0;
        std::vector<int64_t> offsets;   // letture recenti, per la mediana
    };

    struct CapturedNote
    {
        int64_t start, end;
        int pitch, velocity;
    };

    struct Placement;

    // AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    // PhysicalMidiInputDevice::Listener
    void handleIncomingMidiMessage (const juce::MidiMessage&) override;

    void timerCallback() override;
    void attachMidiInputs();

    std::optional<double> editTimeFor (int64_t position) const;
    std::optional<int64_t> positionForEditTime (double seconds) const;
    std::optional<Placement> place (tracktion::AudioTrack&, int64_t start, int64_t end, double latencySeconds) const;

    juce::Result captureMidi (tracktion::AudioTrack&, Destination, recording::Recorder*, tracktion::Clip**);
    juce::Result captureAudio (tracktion::AudioTrack&, tracktion::WaveInputDevice&, Destination,
                               recording::Recorder*, tracktion::Clip**);

    std::vector<CapturedNote> collectNotes() const;
    int64_t positionNow() const;

    tracktion::Engine& engine;
    tracktion::Edit* edit = nullptr;
    const double audioSeconds;
    const double createdMs = juce::Time::getMillisecondCounterHiRes();

    // Audio: anello con tutti i canali d'ingresso del dispositivo
    juce::AudioBuffer<float> ring;
    std::atomic<int64_t> devicePosition { 0 };
    std::atomic<double> lastCallbackMs { 0.0 };
    std::atomic<int64_t> lastCallbackPosition { 0 };
    std::atomic<double> sampleRate { 44100.0 };
    static constexpr int maxChannels = 32;
    std::array<std::atomic<int64_t>, maxChannels> lastLoud {};
    std::atomic<bool> running { false };
    std::map<juce::String, int64_t> audioConsumedUpTo;

    // MIDI
    mutable juce::CriticalSection midiLock;
    std::vector<MidiEvent> midiEvents;
    int64_t midiConsumedUpTo = -1;
    std::vector<std::shared_ptr<tracktion::MidiInputDevice>> listenedInputs;

    // Allineamento al trasporto
    std::vector<Segment> segments;
    bool wasPlaying = false;
    bool batch = false;
    int ticks = 0;
};

} // namespace sonora::capture
