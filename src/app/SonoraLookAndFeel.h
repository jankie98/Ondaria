/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{

/** Palette e stile dell'interfaccia: grigi scuri e un solo accento. */
namespace colours
{
    inline const juce::Colour background   { 0xff1e1f22 };
    inline const juce::Colour panel        { 0xff2a2c30 };
    inline const juce::Colour panelLight   { 0xff35383d };
    inline const juce::Colour outline      { 0xff44484e };
    inline const juce::Colour text         { 0xffe4e6ea };
    inline const juce::Colour textDim      { 0xff9aa0a8 };
    inline const juce::Colour accent       { 0xfff2a93b };
    inline const juce::Colour selection    { 0xff3d5a80 };
}

class SonoraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SonoraLookAndFeel()
    {
        setColourScheme ({ colours::background, colours::panel, colours::panelLight,
                           colours::outline, colours::text, colours::selection,
                           colours::text, colours::accent, colours::text });

        setColour (juce::ListBox::backgroundColourId, colours::panel);
        setColour (juce::TextEditor::backgroundColourId, colours::background);
        setColour (juce::TextEditor::outlineColourId, colours::outline);
        setColour (juce::TextButton::buttonColourId, colours::panelLight);
        setColour (juce::TextButton::buttonOnColourId, colours::accent);
        setColour (juce::TextButton::textColourOnId, colours::background);
        setColour (juce::Label::textColourId, colours::text);
    }
};

} // namespace sonora
