/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

/** Piccoli strumenti per le animazioni dell'interfaccia. */
namespace sonora::anim
{

/** Curva che rallenta alla fine (t da 0 a 1). */
inline float easeOut (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

/** Mostra un componente con una dissolvenza. */
inline void fadeIn (juce::Component& c, int ms = 160)
{
    c.setAlpha (0.0f);
    c.setVisible (true);
    juce::Desktop::getInstance().getAnimator().animateComponent (&c, c.getBounds(), 1.0f, ms, false, 1.0, 0.0);
}

/** Avvicina value a target di una frazione per fotogramma; true se si muove ancora. */
inline bool approach (float& value, float target, float speed = 0.25f)
{
    if (std::abs (target - value) < 0.5f)
    {
        const bool moved = ! juce::approximatelyEqual (value, target);
        value = target;
        return moved;
    }

    value += (target - value) * speed;
    return true;
}

//==============================================================================
/** Testina disegnata su un componente sottile che si sposta a ogni fotogramma:
    scorre fluida senza ridisegnare tutta la vista. */
class PlayheadOverlay final : public juce::Component
{
public:
    /** getX: posizione della testina nel genitore, o nullopt se non va mostrata.
        isPlaying: verde in riproduzione, chiara da ferma. */
    PlayheadOverlay (std::function<std::optional<int>()> getXIn, std::function<bool()> isPlayingIn)
        : getX (std::move (getXIn)), isPlaying (std::move (isPlayingIn))
    {
        setInterceptsMouseClicks (false, false);
    }

    /** Fascia verticale (nel genitore) dove scorre la testina. */
    void setArea (juce::Rectangle<int> a)       { area = a; update(); }

    void update()
    {
        const auto x = getX ? getX() : std::nullopt;
        const bool playing = isPlaying && isPlaying();

        if (! x || *x < area.getX() || *x > area.getRight())
        {
            // Resta visibile (con dimensione nulla) per continuare a ricevere i fotogrammi
            if (! getBounds().isEmpty())
                setBounds ({});

            return;
        }

        const auto bounds = juce::Rectangle<int> (*x - halfWidth, area.getY(), halfWidth * 2 + 1, area.getHeight());

        if (bounds != getBounds())
            setBounds (bounds);

        if (playing != wasPlaying)
        {
            wasPlaying = playing;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto colour = wasPlaying ? colours::play : colours::text;
        const float cx = (float) halfWidth + 0.5f;

        if (wasPlaying)
        {
            // Alone morbido attorno alla linea
            juce::ColourGradient glow (colour.withAlpha (0.0f), 0.0f, 0.0f, colour.withAlpha (0.0f), (float) getWidth(), 0.0f, false);
            glow.addColour (0.5, colour.withAlpha (0.18f));
            g.setGradientFill (glow);
            g.fillRect (getLocalBounds().withTrimmedTop (7));
        }

        g.setColour (colour);
        g.fillRect (juce::Rectangle<float> (cx - 0.5f, 0.0f, 1.0f, (float) getHeight()));

        juce::Path head;
        head.addTriangle (cx - 5.0f, 0.0f, cx + 5.0f, 0.0f, cx, 7.0f);
        g.fillPath (head);
    }

private:
    static constexpr int halfWidth = 6;
    std::function<std::optional<int>()> getX;
    std::function<bool()> isPlaying;
    juce::Rectangle<int> area;
    bool wasPlaying = false;
    juce::VBlankAttachment vblank { this, [this] { update(); } };
};

} // namespace sonora::anim
