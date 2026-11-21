/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Theme.h"
#include "session/Freeze.h"

/** Pezzi d'interfaccia del freeze usati da piu' viste. */
namespace sonora::freezeui
{

inline juce::String frozenNotice (const tracktion::AudioTrack& t)
{
    return juce::String::fromUTF8 ("\"") + t.getName()
         + juce::String::fromUTF8 ("\" \xc3\xa8 congelata: scongelala per modificarla");
}

/** Colore "ghiaccio" delle tracce congelate. */
inline juce::Colour iceColour()    { return juce::Colour (0xff9fd8ff); }

/** Velo e righe oblique sopra le clip di una traccia congelata. */
inline void drawFrozenVeil (juce::Graphics& g, juce::Rectangle<float> area, float corner = 4.0f)
{
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (area, corner);
    g.reduceClipRegion (clip);

    g.setColour (iceColour().withAlpha (0.16f));
    g.fillRect (area);

    g.setColour (iceColour().withAlpha (0.18f));

    for (float x = area.getX() - area.getHeight(); x < area.getRight(); x += 7.0f)
        g.drawLine (x, area.getBottom(), x + area.getHeight(), area.getY(), 1.0f);
}

/** Fiocco di neve stilizzato (sei raggi con le punte). */
inline void drawSnowflake (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
{
    const auto centre = area.getCentre();
    const auto r = std::min (area.getWidth(), area.getHeight()) * 0.5f;
    juce::Path p;

    for (int i = 0; i < 6; ++i)
    {
        const auto a = juce::MathConstants<float>::pi * (float) i / 3.0f;
        const juce::Point<float> dir (std::sin (a), -std::cos (a));
        const auto tip = centre + dir * r;
        p.startNewSubPath (centre);
        p.lineTo (tip);

        const auto mid = centre + dir * (r * 0.62f);
        const juce::Point<float> side (dir.y, -dir.x);
        p.startNewSubPath (mid + (side - dir) * (r * 0.26f));
        p.lineTo (mid);
        p.lineTo (mid + (-side - dir) * (r * 0.26f));
    }

    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** Voci "Congela / Scongela / Appiattisci" del menu di una traccia. */
inline void addMenuItems (juce::PopupMenu& m, tracktion::AudioTrack& track,
                          std::function<void (const juce::String&)> status)
{
    tracktion::AudioTrack::Ptr t (&track);

    if (freeze::isFrozen (track))
    {
        m.addItem ("Scongela traccia", [t, status]
        {
            freeze::unfreeze (*t);

            if (status)
                status ("Scongelata " + t->getName() + ": i device tornano attivi");
        });
    }
    else
    {
        juce::String why;
        const bool can = freeze::canFreeze (track, &why);
        juce::PopupMenu::Item item ("Congela traccia");
        item.setEnabled (can);
        item.setAction ([t, status]
        {
            auto r = freeze::freeze (*t);

            if (status)
                status (r.wasOk() ? "Congelata " + t->getName() + ": suona dal file, i device non usano CPU"
                                  : r.getErrorMessage());
        });
        m.addItem (std::move (item));
    }

    juce::PopupMenu::Item flat ("Appiattisci (trasforma in audio)");
    flat.setEnabled (freeze::isFrozen (track) || freeze::canFreeze (track));
    flat.setAction ([t, status]
    {
        t->edit.getUndoManager().beginNewTransaction ("Appiattisci " + t->getName());
        auto r = freeze::flatten (*t);

        if (status)
            status (r.wasOk() ? "Appiattita " + t->getName() + juce::String::fromUTF8 (": ora \xc3\xa8 una clip audio (Ctrl+Z per tornare indietro)")
                              : r.getErrorMessage());
    });
    m.addItem (std::move (flat));
}

} // namespace sonora::freezeui
