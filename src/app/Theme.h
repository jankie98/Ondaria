/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{

/** Palette del tema scuro: superfici neutre, il colore lo portano clip e stato. */
namespace colours
{
    inline const juce::Colour background   { 0xff141518 };
    inline const juce::Colour sidebar      { 0xff17181b };
    inline const juce::Colour panel        { 0xff1c1d21 };
    inline const juce::Colour panelLight   { 0xff25272c };
    inline const juce::Colour hover        { 0xff2e3036 };
    inline const juce::Colour divider      { 0xff202227 };
    inline const juce::Colour outline      { 0xff33363d };
    inline const juce::Colour emptySlot    { 0xff1a1b1f };
    inline const juce::Colour text         { 0xffe8e9ec };
    inline const juce::Colour textMid      { 0xffb4b8c0 };
    inline const juce::Colour textDim      { 0xff8b8f98 };
    inline const juce::Colour accent       { 0xff7c9cff };
    inline const juce::Colour play         { 0xff4ade80 };
    inline const juce::Colour record       { 0xfff87171 };
    inline const juce::Colour warning      { 0xfff2a93b };
    inline const juce::Colour selection    { 0xff2a2c32 };
}

/** Misure ricorrenti, su una griglia da 4 px. */
namespace metrics
{
    inline constexpr int topBarHeight   = 52;
    inline constexpr int browserWidth   = 248;
    inline constexpr int detailHeight   = 244;
    inline constexpr int slotHeight     = 28;
    inline constexpr int gap            = 4;
    inline constexpr float radius       = 6.0f;
}

/** Caratteri dell'interfaccia: Inter per il testo, JetBrains Mono per numeri e tempi. */
namespace fonts
{
    enum class Weight { regular, medium, semibold };

    juce::Font ui (float height, Weight = Weight::regular);
    juce::Font mono (float height);
}

class SonoraLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SonoraLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override     { return 8; }

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    juce::Font getLabelFont (juce::Label&) override;
};

} // namespace sonora
