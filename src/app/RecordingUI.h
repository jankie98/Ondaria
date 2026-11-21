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

/** Anteprima di una registrazione in corso: forma d'onda dell'audio o note MIDI,
    disegnate nell'area che va dall'inizio della presa alla testina.
    xForTime converte un tempo dell'edit in x (nelle coordinate di area). */
inline void drawLiveTake (juce::Graphics& g, const recording::LiveTake& take, juce::Rectangle<float> area,
                          tracktion::TimePosition now, std::function<float (tracktion::TimePosition)> xForTime,
                          juce::Colour colour)
{
    if (area.getWidth() < 2.0f || area.getHeight() < 4.0f)
        return;

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (area.toNearestInt());

    if (take.audio != nullptr)
    {
        const auto length = (now - take.start).inSeconds();

        if (length > 0.0)
        {
            g.setColour (colour);
            take.audio->thumb->drawChannels (g, area.toNearestInt(), 0.0, length, 1.0f);
        }
    }

    if (take.notes.empty())
        return;

    int lo = 127, hi = 0;

    for (auto& n : take.notes)
    {
        lo = std::min (lo, n.pitch);
        hi = std::max (hi, n.pitch);
    }

    // Almeno un'ottava di altezza, cosi' poche note non riempiono tutta la corsia
    const int range = std::max (12, hi - lo + 1);
    const int bottom = lo - (range - (hi - lo + 1)) / 2;
    const float rowH = std::max (2.0f, area.getHeight() / (float) range);
    g.setColour (colour);

    for (auto& n : take.notes)
    {
        const float x1 = xForTime (n.start);
        const float x2 = std::max (x1 + 3.0f, xForTime (n.held ? now : n.end));
        const float y = area.getBottom() - area.getHeight() * (float) (n.pitch - bottom + 1) / (float) range;
        g.fillRoundedRectangle ({ x1, y, x2 - x1, std::min (rowH, 4.0f) }, 1.0f);
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
