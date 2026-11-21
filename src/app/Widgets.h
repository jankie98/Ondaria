/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "Theme.h"

namespace sonora
{

/** Icone vettoriali disegnate a tratto, in un riquadro 0..1. */
struct Icon
{
    juce::Path path;
    bool filled = false;   ///< true: riempita, false: disegnata a tratto
};

namespace icons
{
    Icon play();
    Icon stop();
    Icon record();
    Icon search();
    Icon plus();
    Icon undo();
    Icon redo();
    Icon metronome();
    Icon keyboard();
    Icon menu();
    Icon close();
    Icon power();
    Icon folder();
    Icon loop();
    Icon capture();

    /** Disegna un'icona nel rettangolo dato. */
    void draw (juce::Graphics&, const Icon&, juce::Rectangle<float>, juce::Colour);
}

//==============================================================================
/** Pulsante piatto con icona: sfondo solo al passaggio del mouse o se attivo. */
class IconButton : public juce::Button
{
public:
    IconButton (const juce::String& name, Icon icon);

    void setIconColour (juce::Colour c)             { iconColour = c; repaint(); }
    void setActiveColour (juce::Colour c)           { activeColour = c; repaint(); }
    void setDrawsBackground (bool b)                { drawsBackground = b; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Icon icon;
    bool drawsBackground = false;
    juce::Colour iconColour { colours::textMid }, activeColour { colours::accent };
};

//==============================================================================
/** Pulsante di testo piatto (es. "Session", "M", "S"), con stato attivo colorato. */
class FlatButton : public juce::Button
{
public:
    explicit FlatButton (const juce::String& buttonText, float initialFontSize = 12.5f);

    void setOnColours (juce::Colour background, juce::Colour textColour)   { onBackground = background; onText = textColour; repaint(); }
    void setBaseColour (juce::Colour c)                              { baseColour = c; repaint(); }
    void setFontSize (float s)                                       { fontSize = s; repaint(); }

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    float fontSize;
    juce::Colour baseColour { juce::Colours::transparentBlack };
    juce::Colour onBackground { colours::hover }, onText { colours::text };
};

//==============================================================================
/** Numero modificabile trascinando in verticale; doppio clic per scriverlo. */
class DragNumber final : public juce::Component
{
public:
    DragNumber (double minValue, double maxValue, double step, int decimals, juce::String suffix = {});

    void setValue (double, juce::NotificationType = juce::sendNotificationSync);
    double getValue() const     { return value; }

    std::function<void (double)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    double minValue, maxValue, step, value = 0, dragStartValue = 0;
    int decimals;
    juce::String suffix;
    std::unique_ptr<juce::TextEditor> editor;
};

//==============================================================================
/** Misuratore di livello verticale stereo, collegato a un LevelMeasurer. */
class LevelMeter final : public juce::Component,
                         private juce::Timer
{
public:
    LevelMeter();
    ~LevelMeter() override;

    /** Collega il misuratore (nullptr per scollegarlo). */
    void setMeasurer (tracktion::LevelMeasurer*);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    tracktion::LevelMeasurer* measurer = nullptr;
    tracktion::LevelMeasurer::Client client;
    float levels[2] { -100.0f, -100.0f };
    float peaks[2] { -100.0f, -100.0f };
    int peakHold[2] { 0, 0 };
};

} // namespace sonora
