/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace sonora
{

/** Disegno dal vivo di un device integrato dentro la sua scheda: curva dell'EQ
    e del filtro (con i punti da trascinare), curva del compressore, ripetizioni
    del delay, coda del riverbero, LFO in movimento, inviluppo del synth.
*/
class DeviceVisualiser : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    /** nullptr se il device non ha un disegno (per esempio un plugin esterno). */
    static std::unique_ptr<DeviceVisualiser> create (tracktion::Plugin&, juce::Colour);

    explicit DeviceVisualiser (juce::Colour c) : colour (c) {}

    /** Chiamato a ogni fotogramma della catena: ridisegna se qualcosa e' cambiato. */
    virtual void refresh() = 0;

    static constexpr int preferredWidth = 212;

protected:
    juce::Colour colour;

    /** Fondo scuro arrotondato su cui disegnare. */
    void paintBackground (juce::Graphics&) const;

    /** Area utile del disegno, dentro al bordo. */
    juce::Rectangle<float> plotArea() const     { return getLocalBounds().toFloat().reduced (6.0f, 6.0f); }
};

} // namespace sonora
