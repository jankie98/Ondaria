/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Widgets.h"

namespace te = tracktion;

namespace sonora
{

//==============================================================================
namespace icons
{
    Icon play()
    {
        juce::Path p;
        p.addTriangle (0.2f, 0.1f, 0.9f, 0.5f, 0.2f, 0.9f);
        return { p, true };
    }

    Icon stop()
    {
        juce::Path p;
        p.addRoundedRectangle (0.15f, 0.15f, 0.7f, 0.7f, 0.1f);
        return { p, true };
    }

    Icon record()
    {
        juce::Path p;
        p.addEllipse (0.12f, 0.12f, 0.76f, 0.76f);
        return { p, true };
    }

    Icon search()
    {
        juce::Path p;
        p.addEllipse (0.08f, 0.08f, 0.62f, 0.62f);
        p.startNewSubPath (0.62f, 0.62f);
        p.lineTo (0.92f, 0.92f);
        return { p, false };
    }

    Icon plus()
    {
        juce::Path p;
        p.startNewSubPath (0.5f, 0.15f); p.lineTo (0.5f, 0.85f);
        p.startNewSubPath (0.15f, 0.5f); p.lineTo (0.85f, 0.5f);
        return { p, false };
    }

    Icon undo()
    {
        juce::Path p;
        p.startNewSubPath (0.3f, 0.2f);
        p.lineTo (0.1f, 0.4f);
        p.lineTo (0.3f, 0.6f);
        p.startNewSubPath (0.1f, 0.4f);
        p.lineTo (0.62f, 0.4f);
        p.quadraticTo (0.92f, 0.4f, 0.92f, 0.65f);
        p.quadraticTo (0.92f, 0.88f, 0.62f, 0.88f);
        p.lineTo (0.4f, 0.88f);
        return { p, false };
    }

    Icon redo()
    {
        auto i = undo();
        i.path.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f).translated (1.0f, 0.0f));
        return i;
    }

    Icon metronome()
    {
        juce::Path p;
        p.startNewSubPath (0.38f, 0.1f);
        p.lineTo (0.62f, 0.1f);
        p.lineTo (0.82f, 0.9f);
        p.lineTo (0.18f, 0.9f);
        p.closeSubPath();
        p.startNewSubPath (0.5f, 0.72f);
        p.lineTo (0.78f, 0.28f);
        return { p, false };
    }

    Icon keyboard()
    {
        juce::Path p;
        p.addRoundedRectangle (0.05f, 0.22f, 0.9f, 0.56f, 0.08f);

        for (float x : { 0.275f, 0.5f, 0.725f })
        {
            p.startNewSubPath (x, 0.22f);
            p.lineTo (x, 0.78f);
        }

        return { p, false };
    }

    Icon menu()
    {
        juce::Path p;

        for (float y : { 0.25f, 0.5f, 0.75f })
        {
            p.startNewSubPath (0.15f, y);
            p.lineTo (0.85f, y);
        }

        return { p, false };
    }

    Icon close()
    {
        juce::Path p;
        p.startNewSubPath (0.22f, 0.22f); p.lineTo (0.78f, 0.78f);
        p.startNewSubPath (0.78f, 0.22f); p.lineTo (0.22f, 0.78f);
        return { p, false };
    }

    Icon power()
    {
        juce::Path p;
        p.addCentredArc (0.5f, 0.55f, 0.36f, 0.36f, 0.0f,
                         juce::MathConstants<float>::pi * 0.22f, juce::MathConstants<float>::pi * 1.78f, true);
        p.startNewSubPath (0.5f, 0.08f);
        p.lineTo (0.5f, 0.5f);
        return { p, false };
    }

    Icon folder()
    {
        juce::Path p;
        p.startNewSubPath (0.08f, 0.2f);
        p.lineTo (0.4f, 0.2f);
        p.lineTo (0.5f, 0.32f);
        p.lineTo (0.92f, 0.32f);
        p.lineTo (0.92f, 0.82f);
        p.lineTo (0.08f, 0.82f);
        p.closeSubPath();
        return { p, false };
    }

    Icon loop()
    {
        juce::Path p;
        p.startNewSubPath (0.2f, 0.62f);
        p.lineTo (0.2f, 0.32f);
        p.lineTo (0.78f, 0.32f);
        p.startNewSubPath (0.66f, 0.2f);
        p.lineTo (0.78f, 0.32f);
        p.lineTo (0.66f, 0.44f);
        p.startNewSubPath (0.8f, 0.38f);
        p.lineTo (0.8f, 0.68f);
        p.lineTo (0.22f, 0.68f);
        p.startNewSubPath (0.34f, 0.56f);
        p.lineTo (0.22f, 0.68f);
        p.lineTo (0.34f, 0.8f);
        return { p, false };
    }
}

void icons::draw (juce::Graphics& g, const Icon& icon, juce::Rectangle<float> area, juce::Colour c)
{
    auto p = icon.path;
    p.applyTransform (juce::AffineTransform::scale (area.getWidth(), area.getHeight()).translated (area.getX(), area.getY()));
    g.setColour (c);

    if (icon.filled)
        g.fillPath (p);
    else
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
IconButton::IconButton (const juce::String& name, Icon i)
    : juce::Button (name), icon (std::move (i))
{
    setTooltip (name);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();

    if (drawsBackground || highlighted || down)
    {
        g.setColour (down ? colours::hover.brighter (0.08f) : (highlighted ? colours::hover : colours::panel));
        g.fillRoundedRectangle (r, metrics::radius);
    }

    const auto size = juce::jmin (r.getWidth(), r.getHeight()) * 0.42f;
    auto colour = getToggleState() ? activeColour : (highlighted ? colours::text : iconColour);

    if (! isEnabled())
        colour = colour.withMultipliedAlpha (0.35f);

    icons::draw (g, icon, r.withSizeKeepingCentre (size, size), colour);
}

//==============================================================================
FlatButton::FlatButton (const juce::String& buttonText, float initialFontSize)
    : juce::Button (buttonText), fontSize (initialFontSize)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void FlatButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat();
    auto bg = getToggleState() ? onBackground : baseColour;

    if (down)
        bg = (bg.isTransparent() ? colours::hover : bg).brighter (0.1f);
    else if (highlighted && ! getToggleState())
        bg = colours::hover;

    if (! bg.isTransparent())
    {
        g.setColour (bg);
        g.fillRoundedRectangle (r, metrics::radius);
    }

    g.setColour (getToggleState() ? onText : (highlighted ? colours::text : colours::textMid));

    if (! isEnabled())
        g.setColour (colours::textDim.withMultipliedAlpha (0.6f));

    g.setFont (fonts::ui (fontSize, getToggleState() ? fonts::Weight::medium : fonts::Weight::regular));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred, true);
}

//==============================================================================
DragNumber::DragNumber (double minV, double maxV, double s, int d, juce::String suf)
    : minValue (minV), maxValue (maxV), step (s), value (minV), decimals (d), suffix (std::move (suf))
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    setWantsKeyboardFocus (false);
}

void DragNumber::setValue (double v, juce::NotificationType n)
{
    v = juce::jlimit (minValue, maxValue, std::round (v / step) * step);

    if (juce::approximatelyEqual (v, value))
        return;

    value = v;
    repaint();

    if (n != juce::dontSendNotification && onChange)
        onChange (value);
}

void DragNumber::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), metrics::radius);

    if (editor != nullptr)
        return;

    auto r = getLocalBounds().reduced (8, 0);
    const auto number = juce::String (value, decimals);
    auto numberFont = fonts::mono (13.0f);
    auto suffixFont = fonts::ui (11.0f);

    const float suffixWidth = suffix.isEmpty() ? 0.0f
                                               : juce::GlyphArrangement::getStringWidth (suffixFont, suffix) + 5.0f;
    const float numberWidth = juce::GlyphArrangement::getStringWidth (numberFont, number);
    auto x = (float) r.getX() + ((float) r.getWidth() - numberWidth - suffixWidth) / 2.0f;

    g.setColour (colours::text);
    g.setFont (numberFont);
    g.drawText (number, juce::Rectangle<float> (x, 0.0f, numberWidth + 1.0f, (float) getHeight()), juce::Justification::centredLeft);

    if (suffix.isNotEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (suffixFont);
        g.drawText (suffix, juce::Rectangle<float> (x + numberWidth + 5.0f, 0.0f, suffixWidth, (float) getHeight()),
                    juce::Justification::centredLeft);
    }
}

void DragNumber::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds());
}

void DragNumber::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;
}

void DragNumber::mouseDrag (const juce::MouseEvent& e)
{
    const double sensitivity = e.mods.isShiftDown() ? step * 0.1 : step;
    setValue (dragStartValue - e.getDistanceFromDragStartY() * juce::jmax (sensitivity, (maxValue - minValue) / 2000.0));
}

void DragNumber::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    setValue (value + (w.deltaY > 0 ? step : -step) * (decimals > 0 ? 10.0 : 1.0));
}

void DragNumber::mouseDoubleClick (const juce::MouseEvent&)
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (fonts::mono (13.0f));
    editor->setJustification (juce::Justification::centred);
    editor->setText (juce::String (value, decimals), false);
    editor->selectAll();
    editor->setInputRestrictions (8, "0123456789.,");

    auto finish = [this] (bool apply)
    {
        if (editor == nullptr)
            return;

        if (apply)
            setValue (editor->getText().replaceCharacter (',', '.').getDoubleValue());

        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
        {
            if (safe != nullptr)
            {
                safe->editor.reset();
                safe->repaint();
            }
        });
    };

    editor->onReturnKey = [finish] { finish (true); };
    editor->onFocusLost = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };

    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
}

//==============================================================================
LevelMeter::LevelMeter()
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

LevelMeter::~LevelMeter()
{
    setMeasurer (nullptr);
}

void LevelMeter::setMeasurer (te::LevelMeasurer* m)
{
    if (measurer == m)
        return;

    if (measurer != nullptr)
        measurer->removeClient (client);

    measurer = m;

    if (measurer != nullptr)
        measurer->addClient (client);
}

void LevelMeter::timerCallback()
{
    bool changed = false;

    for (int ch = 0; ch < 2; ++ch)
    {
        const float target = measurer != nullptr ? client.getAndClearAudioLevel (ch).dB : -100.0f;
        // Salita immediata, discesa morbida.
        const float next = target > levels[ch] ? target : juce::jmax (-100.0f, levels[ch] - 1.6f);

        if (! juce::approximatelyEqual (next, levels[ch]))
        {
            levels[ch] = next;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float barWidth = (r.getWidth() - 2.0f) / 2.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float> (r.getX() + (float) ch * (barWidth + 2.0f), r.getY(), barWidth, r.getHeight());
        g.setColour (colours::panelLight);
        g.fillRoundedRectangle (bar, 2.0f);

        // Scala da -60 a +6 dB.
        const float proportion = juce::jlimit (0.0f, 1.0f, (levels[ch] + 60.0f) / 66.0f);

        if (proportion > 0.0f)
        {
            auto filled = bar.withTop (bar.getBottom() - bar.getHeight() * proportion);
            g.setColour (levels[ch] > 0.0f ? colours::record : (levels[ch] > -6.0f ? colours::warning : colours::play));
            g.fillRoundedRectangle (filled, 2.0f);
        }
    }
}

} // namespace sonora
