/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

namespace sonora
{

/** Palette attiva: superfici neutre, il colore lo portano clip e stato.
    I valori cambiano con theme::apply (scuro o chiaro): leggili al momento di
    disegnare, non copiarli in membri. */
namespace colours
{
    inline juce::Colour background   { 0xff141518 };
    inline juce::Colour sidebar      { 0xff17181b };
    inline juce::Colour panel        { 0xff1c1d21 };
    inline juce::Colour panelLight   { 0xff25272c };
    inline juce::Colour hover        { 0xff2e3036 };
    inline juce::Colour divider      { 0xff202227 };
    inline juce::Colour outline      { 0xff33363d };
    inline juce::Colour emptySlot    { 0xff1a1b1f };
    inline juce::Colour text         { 0xffe8e9ec };
    inline juce::Colour textMid      { 0xffb4b8c0 };
    inline juce::Colour textDim      { 0xff8b8f98 };
    inline juce::Colour accent       { 0xff7c9cff };
    inline juce::Colour play         { 0xff4ade80 };
    inline juce::Colour record       { 0xfff87171 };
    inline juce::Colour warning      { 0xfff2a93b };
    inline juce::Colour selection    { 0xff2a2c32 };

    /** Testo e icone sopra un colore pieno (clip, pulsanti accesi): scuro in tutti i temi. */
    inline juce::Colour ink          { 0xff121316 };
}

/** Temi dell'interfaccia. */
namespace theme
{
    enum class Kind { dark, light };

    Kind current();
    juce::String name (Kind);

    /** Cambia la palette, aggiorna il LookAndFeel e ridisegna tutte le finestre.
        I componenti con colori impostati a mano li riprendono in lookAndFeelChanged. */
    void apply (Kind);

    /** Un colore preso dalla palette (anche di un altro tema) diventa il suo
        corrispondente nella palette attiva, alpha compresa; gli altri restano uguali.
        Per i colori salvati nei membri dei widget. */
    juce::Colour follow (juce::Colour);

    /** Tema salvato nelle impostazioni (scuro se mai scelto). */
    Kind load (juce::PropertySet&);
    void save (juce::PropertySet&, Kind);
}

/** Modalita' performance: per suonare dal vivo. Solo la session, slot piu' grandi
    da centrare al volo, niente animazioni continue che non servono. */
namespace performance
{
    inline bool enabled = false;

    inline int slotHeight()     { return enabled ? 44 : 28; }
    inline int columnWidth()    { return enabled ? 168 : 132; }
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

    /** Ricarica i colori dei widget dalla palette attiva. */
    void refreshColours();

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
