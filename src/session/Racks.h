/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

/** Rack di device: catene di device in parallelo dentro un solo device della traccia,
    con otto macro che muovono insieme i parametri dei device dentro.

    Sotto c'e' un RackType di Tracktion. Le catene sono un modello nostro salvato nello
    stato del rack (ordine dei device e un volume/pan in fondo a ogni catena); i
    collegamenti del rack si ricalcolano da li' a ogni modifica. In ogni catena il
    segnale entra dall'ingresso del rack, passa per i device in ordine e per il volume
    della catena; le uscite delle catene si sommano. Il MIDI arriva a tutti i device,
    cosi' un rack puo' contenere anche strumenti (una catena per suono).

    Ogni funzione che modifica qualcosa va chiamata dentro una transazione dell'undo
    (beginNewTransaction): il rack e' tutto nello stato dell'Edit, quindi l'undo
    riporta indietro catene e collegamenti insieme.
*/
namespace sonora::racks
{
    constexpr int numMacros = 8;

    /** Il device e' un rack con le catene di Sonora. */
    bool isRack (const tracktion::Plugin&);

    /** Il RackType di un device rack (nullptr se non e' un rack di Sonora). */
    tracktion::RackType* getRackType (const tracktion::Plugin&);

    /** Il rack che contiene questo device (nullptr se e' direttamente sulla traccia). */
    tracktion::RackType* getContainingRack (const tracktion::Plugin&);

    /** Il device rack sulla traccia che usa questo RackType. */
    tracktion::Plugin* findRackInstance (tracktion::Track&, const tracktion::RackType&);

    //==============================================================================
    /** Nuovo rack con una catena vuota, nella posizione index della catena della traccia. */
    tracktion::Plugin::Ptr createRack (tracktion::AudioTrack&, int index, const juce::String& name = {});

    /** Mette questi device della traccia in un nuovo rack con una catena, al posto del
        primo. I device restano nell'ordine che hanno sulla traccia. */
    tracktion::Plugin::Ptr group (tracktion::AudioTrack&, const tracktion::Plugin::Array&);

    /** Si puo' separare: il rack ha una sola catena. */
    bool canUngroup (const tracktion::Plugin& rack);

    /** Rimette sulla traccia i device dell'unica catena, al posto del rack. */
    bool ungroup (tracktion::Plugin& rack);

    /** Toglie il rack dalla traccia, device compresi. */
    void deleteRack (tracktion::Plugin& rack);

    /** Toglie dallo stato di un progetto (prima di aprirlo) i rack che nessuna traccia
        usa piu', rimasti per poter annullare una separazione o un'eliminazione. */
    void removeUnusedRacks (juce::ValueTree& editState);

    //==============================================================================
    int getNumChains (const tracktion::RackType&);

    /** Aggiunge una catena vuota in fondo; restituisce il suo indice. */
    int addChain (tracktion::RackType&, const juce::String& name = {});

    /** Toglie la catena con i suoi device (ne resta sempre almeno una). */
    void removeChain (tracktion::RackType&, int chain);

    /** Copia la catena (device e impostazioni) subito dopo l'originale. */
    int duplicateChain (tracktion::RackType&, int chain);

    juce::String getChainName (const tracktion::RackType&, int chain);
    void setChainName (tracktion::RackType&, int chain, const juce::String&);

    /** I device della catena, in ordine (senza il volume finale). */
    tracktion::Plugin::Array getChainPlugins (tracktion::RackType&, int chain);

    /** Il volume/pan in fondo alla catena. */
    tracktion::VolumeAndPanPlugin* getChainFader (tracktion::RackType&, int chain);

    /** La catena che contiene il device, -1 se nessuna. */
    int findChainOf (tracktion::RackType&, const tracktion::Plugin&);

    bool isChainMuted (const tracktion::RackType&, int chain);
    void setChainMuted (tracktion::RackType&, int chain, bool);
    bool isChainSoloed (const tracktion::RackType&, int chain);
    void setChainSoloed (tracktion::RackType&, int chain, bool);

    /** La catena si sente: non e' muta e, se qualcuna e' in solo, e' tra quelle. */
    bool isChainAudible (const tracktion::RackType&, int chain);

    /** La catena mostrata nel pannello dei device (non entra nell'undo). */
    int getSelectedChain (const tracktion::RackType&);
    void setSelectedChain (tracktion::RackType&, int chain);

    /** Mette un device nella catena alla posizione data (-1: in fondo). Se il device e'
        gia' da un'altra parte (traccia o altro rack) ce lo sposta. */
    bool insertPlugin (tracktion::RackType&, int chain, const tracktion::Plugin::Ptr&, int position = -1);

    /** Toglie ed elimina un device da una catena. */
    void removePlugin (tracktion::RackType&, tracktion::Plugin&);

    /** Ricalcola i collegamenti del rack dalle catene. Lo fanno gia' tutte le funzioni
        qui sopra; serve se lo stato e' stato toccato a mano. */
    void rewire (tracktion::RackType&);

    //==============================================================================
    /** Le otto macro del rack (le crea se mancano). */
    juce::ReferenceCountedArray<tracktion::MacroParameter> getMacros (tracktion::RackType&);
    tracktion::MacroParameter* getMacro (tracktion::RackType&, int index);

    juce::String getMacroName (tracktion::RackType&, int index);
    void setMacroName (tracktion::RackType&, int index, const juce::String&);

    /** Valore della macro, 0..1. */
    float getMacroValue (tracktion::RackType&, int index);
    void setMacroValue (tracktion::RackType&, int index, float);

    struct Mapping
    {
        tracktion::AutomatableParameter::Ptr parameter;
        float minimum = 0.0f, maximum = 1.0f;   ///< valori normalizzati del parametro con la macro a 0 e a 1
    };

    /** I parametri mossi dalla macro, con il loro intervallo. */
    std::vector<Mapping> getMappings (tracktion::RackType&, int macro);

    /** Collega un parametro di un device del rack alla macro, su tutto il suo intervallo
        (un parametro va su una sola macro: se era su un'altra, passa a questa). */
    bool map (tracktion::RackType&, int macro, tracktion::AutomatableParameter&);

    /** Cambia l'intervallo: con la macro a 0 il parametro vale minimum, a 1 maximum
        (normalizzati; minimum > maximum inverte il verso). */
    void setMappingRange (tracktion::RackType&, int macro, tracktion::AutomatableParameter&, float minimum, float maximum);

    void unmap (tracktion::RackType&, int macro, tracktion::AutomatableParameter&);

    /** La macro del rack che muove il parametro, -1 se nessuna. */
    int getMacroFor (tracktion::RackType&, const tracktion::AutomatableParameter&);
}
