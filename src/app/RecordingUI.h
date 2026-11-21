/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Theme.h"
#include "session/Recording.h"

/** Pezzi d'interfaccia della registrazione usati da piu' viste. */
namespace sonora::recordingui
{

/** Sottomenu "Ingresso" di una traccia. */
inline juce::PopupMenu inputMenu (recording::Recorder& recorder, tracktion::AudioTrack& track)
{
    juce::PopupMenu m;
    const auto current = recording::getInput (track);
    tracktion::AudioTrack::Ptr t (&track);
    juce::WeakReference<recording::Recorder> rec (&recorder);

    for (auto& choice : recording::getInputChoices (track.edit))
    {
        m.addItem (choice.name, true, choice.id == current, [t, rec, id = choice.id]
        {
            recording::setInput (*t, id);

            if (rec != nullptr)
                rec->syncInputs();
        });
    }

    return m;
}

/** Cerchio di armamento: pieno se la traccia e' armata, piu' tenue se lo e' in automatico. */
inline void drawArmButton (juce::Graphics& g, juce::Rectangle<float> area, bool armed, bool autoArmed, bool hover)
{
    auto circle = area.withSizeKeepingCentre (12.0f, 12.0f);

    if (armed)
    {
        g.setColour (colours::record.withAlpha (autoArmed ? 0.55f : 1.0f));
        g.fillEllipse (circle);
    }
    else
    {
        g.setColour (hover ? colours::record : colours::textDim);
        g.drawEllipse (circle.reduced (0.75f), 1.5f);
    }
}

inline juce::String armTooltip (recording::Recorder& recorder, tracktion::AudioTrack& t)
{
    juce::String s = recorder.isAutoArmed (t) && ! recording::isExplicitlyArmed (t)
                        ? "Armata perche' selezionata"
                        : (recorder.isArmed (t) ? "Disarma" : "Arma per la registrazione");
    return s + " (ingresso: " + recording::getInputName (t) + ")";
}

/** Pulsante di armamento di una traccia (stessa forma di M e S nel mixer). */
class ArmButton final : public juce::Button
{
public:
    ArmButton() : juce::Button ("Arma") {}

    bool autoArmed = false;

    void paintButton (juce::Graphics& g, bool highlighted, bool) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (highlighted ? colours::hover : colours::panelLight);
        g.fillRoundedRectangle (r, 4.0f);
        drawArmButton (g, r, getToggleState(), autoArmed, highlighted);
    }
};

} // namespace sonora::recordingui
