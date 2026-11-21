/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Registrazione: ingressi delle tracce, armamento, registrazione
    nell'arrangement e negli slot della session.

    Ogni traccia ha un ingresso ("Tutti gli ingressi MIDI", un ingresso audio o
    nessuno) e un pulsante di armamento. La traccia selezionata con ingresso MIDI
    e' armata in automatico, cosi' la tastiera suona e registra subito.
*/
namespace sonora::recording
{

/** Un ingresso che si puo' scegliere per una traccia. */
struct InputChoice
{
    juce::String id;    ///< "" nessuno, "midi" tutti gli ingressi MIDI, "audio:<nome>"
    juce::String name;  ///< nome da mostrare
};

inline const juce::String midiInputID { "midi" };

/** Ingressi disponibili nel progetto (nessuno, MIDI e gli ingressi audio attivi). */
std::vector<InputChoice> getInputChoices (tracktion::Edit&);

/** Ingresso della traccia (MIDI se non e' mai stato scelto). */
juce::String getInput (const tracktion::AudioTrack&);
juce::String getInputName (const tracktion::AudioTrack&);
void setInput (tracktion::AudioTrack&, const juce::String& inputID);

/** Armamento scelto con il pulsante (salvato nel progetto, non entra nell'undo). */
bool isExplicitlyArmed (const tracktion::AudioTrack&);

/** Una nota suonata durante una registrazione in corso (per l'anteprima). */
struct LiveNote
{
    int pitch = 60;
    tracktion::TimePosition start, end;
    bool held = true;   ///< ancora premuta: la fine segue la testina
};

/** Quello che si sta registrando su una traccia, per disegnarlo mentre arriva. */
struct LiveTake
{
    tracktion::TimePosition start;                                   ///< inizio, in tempo dell'edit
    std::vector<LiveNote> notes;                                     ///< note MIDI arrivate finora
    tracktion::RecordingThumbnailManager::Thumbnail::Ptr audio;      ///< forma d'onda dell'audio
};

//==============================================================================
/** Gestisce la registrazione di un progetto. Ne esiste uno per Edit e va
    distrutto prima dell'Edit. */
class Recorder final : private juce::Timer,
                       private tracktion::MidiInputDevice::MidiKeyChangeDispatcher::Listener
{
public:
    explicit Recorder (tracktion::Edit&);
    ~Recorder() override;

    tracktion::Edit& edit;

    //==============================================================================
    /** La traccia selezionata riceve la tastiera e, se ha l'ingresso MIDI, e' armata in automatico. */
    void setSelectedTrack (tracktion::AudioTrack*);

    /** Armata con il pulsante o in automatico. */
    bool isArmed (const tracktion::AudioTrack&) const;
    bool isAutoArmed (const tracktion::AudioTrack&) const;

    /** Arma o disarma una traccia. Se l'arrangement sta registrando, la traccia entra o esce subito (punch). */
    void setArmed (tracktion::AudioTrack&, bool);

    /** Allinea gli ingressi del motore a ingressi e armamenti delle tracce. */
    void syncInputs();

    //==============================================================================
    /** Pulsante di registrazione dell'arrangement: avvia o ferma la registrazione
        sulle tracce armate (il trasporto parte se e' fermo). */
    juce::Result toggleArrangementRecording();

    /** True se una traccia sta registrando sulla linea del tempo. */
    bool isRecordingInto (const tracktion::AudioTrack&) const;

    /** Inizio della registrazione in corso sulla traccia. */
    std::optional<tracktion::TimePosition> getPunchInTime (const tracktion::AudioTrack&) const;

    //==============================================================================
    /** True se lo slot e' vuoto e la sua traccia e' armata con un ingresso. */
    bool canRecordInto (tracktion::ClipSlot&) const;

    /** Registra nello slot dal prossimo punto di quantizzazione del lancio (o
        subito, facendo partire il trasporto dall'inizio della battuta). */
    juce::Result startSlotRecording (tracktion::ClipSlot&);

    /** Ferma la registrazione dello slot al prossimo punto di quantizzazione; la
        nuova clip parte in loop da quel punto. */
    void stopSlotRecording (tracktion::ClipSlot&);

    enum class SlotRecordState { none, recording, stopping };
    SlotRecordState getSlotRecordState (tracktion::ClipSlot&) const;

    /** Ferma subito le registrazioni negli slot (da chiamare prima di fermare il trasporto). */
    void stopAllSlotRecordings();

    /** True se si sta registrando nell'arrangement o in uno slot. */
    bool isRecording() const;

    /** Registrazione in corso sulla traccia (arrangement o slot), con note e forma
        d'onda arrivate finora; nullptr se la traccia non registra. */
    const LiveTake* getLiveTake (const tracktion::AudioTrack&) const;

    /** Note premute e rilasciate sulla traccia (le chiama il motore; pubblica per i test). */
    void notesReceived (tracktion::AudioTrack&, const juce::Array<int>& on, const juce::Array<int>& off);

    /** Chiamata quando una registrazione in uno slot e' diventata una clip. */
    std::function<void (tracktion::Clip&)> onSlotRecorded;

    /** Fa partire una clip della session come se fosse partita al beat (monotono) clipZero:
        suona subito dal punto giusto del suo loop, in fase con il resto. */
    void launchInPhase (tracktion::Clip&, tracktion::MonotonicBeat clipZero);

private:
    struct PendingAlign
    {
        tracktion::EditItemID clipID;
        tracktion::MonotonicBeat beat;
        int ticksLeft;
        tracktion::BeatDuration wrap {};   // lunghezza del loop: il ritardo si conta modulo questa
    };

    void timerCallback() override;
    void updateLiveTakes();
    void midiKeyStateChanged (tracktion::AudioTrack*, const juce::Array<int>& on, const juce::Array<int>& vels,
                              const juce::Array<int>& off) override;
    void finishSlotClip (tracktion::Clip&, std::optional<tracktion::MonotonicBeat> launchAt, bool launch);
    juce::Array<tracktion::InputDeviceInstance*> inputsFor (const tracktion::AudioTrack&) const;
    void removeIdleSlotTargets();

    tracktion::EditItemID selectedTrackID;
    std::vector<PendingAlign> pendingAligns;
    int ticksUntilSync = 0;
    std::map<tracktion::EditItemID, LiveTake> liveTakes;
    juce::SharedResourcePointer<tracktion::MidiInputDevice::MidiKeyChangeDispatcher> keyDispatcher;

    JUCE_DECLARE_WEAK_REFERENCEABLE (Recorder)
    JUCE_DECLARE_NON_COPYABLE (Recorder)
};

} // namespace sonora::recording
