/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "Widgets.h"

namespace sonora
{

/** Catena di device della traccia selezionata, da sinistra a destra come il
    segnale: ogni device si puo' aprire, bypassare o rimuovere.

    I rack si aprono sul posto: la scheda del rack mostra le catene e le macro, e
    subito dopo vengono i device della catena scelta. Cliccando una catena, i device
    aggiunti dal browser vanno li' invece che sulla traccia.

    - Clic sul titolo di un device: lo seleziona (Maiusc o Ctrl: piu' di uno).
    - Ctrl+G: mette i device selezionati (o tutti) in un rack; Ctrl+Maiusc+G lo separa.
    - Clic destro su una manopola di un device in un rack: assegnala a una macro.
*/
class DeviceChainComponent final : public juce::Component,
                                   public HintProvider,
                                   private juce::Timer
{
public:
    DeviceChainComponent();
    ~DeviceChainComponent() override;

    void setTrack (tracktion::AudioTrack*);

    /** Se e' scelta una catena di un rack, ci mette il device e restituisce true
        (va chiamata dentro una transazione dell'undo). */
    bool insertIntoTarget (const tracktion::Plugin::Ptr&);

    /** Dove vanno i device aggiunti dal browser, per i messaggi ("Rack > Catena 1"). */
    juce::String getInsertTargetName() const;

    /** Ctrl+G: i device selezionati (o tutti quelli della traccia) in un nuovo rack. */
    void groupSelection();

    /** Ctrl+Maiusc+G: separa il rack selezionato (o quello aperto). */
    void ungroupSelection();

    /** Un rack vuoto in fondo alla catena (prima del volume della traccia). */
    void addEmptyRack();

    /** Messaggi per la barra di stato. */
    std::function<void (const juce::String&)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    juce::String getHintAt (juce::Point<int>) override;

private:
    class DeviceBox;
    class RackBox;
    class EmptyChainBox;
    class Content;

    struct Item
    {
        std::unique_ptr<juce::Component> component;
        int width = 0;
        tracktion::RackType* rack = nullptr;   ///< per i device dentro un rack e per la scheda del rack
    };

    void timerCallback() override;
    void rebuild();
    void layoutItems();
    juce::String signature() const;
    tracktion::Plugin::Array getUserPlugins() const;
    juce::Colour trackColour() const;
    bool isTrackFrozen() const;
    bool canEdit();
    int insertionIndex() const;
    void status (const juce::String&);

    void deviceClicked (tracktion::Plugin&, const juce::ModifierKeys&);
    void chainClicked (tracktion::RackType&, int chain);
    void showDeviceMenu (tracktion::Plugin&);
    void showRackMenu (tracktion::Plugin&);
    void clearTarget();
    tracktion::Plugin* targetRackInstance() const;

    tracktion::AudioTrack::Ptr track;
    std::vector<Item> items;
    juce::String shownSignature;
    juce::Array<tracktion::EditItemID> selection;
    tracktion::RackType::Ptr targetRack;
    FlatButton unfreezeButton { "Scongela", 12.0f };
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
};

} // namespace sonora
