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
*/
class DeviceChainComponent final : public juce::Component,
                                   private juce::Timer
{
public:
    DeviceChainComponent();
    ~DeviceChainComponent() override;

    void setTrack (tracktion::AudioTrack*);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class DeviceBox;

    void timerCallback() override;
    void rebuild();
    tracktion::Plugin::Array getUserPlugins() const;
    juce::Colour trackColour() const;
    bool isTrackFrozen() const;

    tracktion::AudioTrack::Ptr track;
    juce::OwnedArray<DeviceBox> boxes;
    tracktion::Plugin::Array shownPlugins;
    juce::Colour shownColour;
    bool shownFrozen = false;
    FlatButton unfreezeButton { "Scongela", 12.0f };
    juce::Viewport viewport;
    juce::Component content;
};

} // namespace sonora
