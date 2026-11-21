/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Theme.h"
#include "SonoraFonts.h"

namespace sonora
{

namespace
{
    struct Typefaces
    {
        juce::Typeface::Ptr regular  = load (SonoraFonts::InterRegular_ttf, SonoraFonts::InterRegular_ttfSize);
        juce::Typeface::Ptr medium   = load (SonoraFonts::InterMedium_ttf, SonoraFonts::InterMedium_ttfSize);
        juce::Typeface::Ptr semibold = load (SonoraFonts::InterSemiBold_ttf, SonoraFonts::InterSemiBold_ttfSize);
        juce::Typeface::Ptr mono     = load (SonoraFonts::JetBrainsMonoRegular_ttf, SonoraFonts::JetBrainsMonoRegular_ttfSize);

        static juce::Typeface::Ptr load (const char* data, int size)
        {
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }

        static Typefaces& get()
        {
            static Typefaces t;
            return t;
        }
    };

    juce::Font makeFont (juce::Typeface::Ptr tf, float height)
    {
        if (tf == nullptr)
            return juce::Font (juce::FontOptions (height));

        return juce::Font (juce::FontOptions (tf).withHeight (height));
    }
}

juce::Font fonts::ui (float height, Weight w)
{
    auto& t = Typefaces::get();
    return makeFont (w == Weight::semibold ? t.semibold : w == Weight::medium ? t.medium : t.regular, height);
}

juce::Font fonts::mono (float height)
{
    return makeFont (Typefaces::get().mono, height);
}

//==============================================================================
//==============================================================================
namespace
{
    theme::Kind currentTheme = theme::Kind::dark;

    struct Palette
    {
        juce::uint32 background, sidebar, panel, panelLight, hover, divider, outline, emptySlot,
                     text, textMid, textDim, accent, play, record, warning, selection;
    };

    constexpr Palette darkPalette  { 0xff141518, 0xff17181b, 0xff1c1d21, 0xff25272c, 0xff2e3036, 0xff202227, 0xff33363d, 0xff1a1b1f,
                                     0xffe8e9ec, 0xffb4b8c0, 0xff8b8f98, 0xff7c9cff, 0xff4ade80, 0xfff87171, 0xfff2a93b, 0xff2a2c32 };

    // Chiaro: carta calda e grigi freddi, accenti un po' piu' scuri per restare leggibili
    constexpr Palette lightPalette { 0xfff3f4f6, 0xffeceef1, 0xffffffff, 0xfff6f7f9, 0xffe3e6eb, 0xffe2e4e8, 0xffcfd3da, 0xffe9ebef,
                                     0xff1b1d22, 0xff464b55, 0xff767b86, 0xff4f6ef7, 0xff16a34a, 0xffdc2626, 0xffd97706, 0xffdde4ff };
}

theme::Kind theme::current()        { return currentTheme; }

juce::String theme::name (Kind k)   { return k == Kind::light ? "chiaro" : "scuro"; }

void theme::apply (Kind k)
{
    currentTheme = k;
    const auto& p = k == Kind::light ? lightPalette : darkPalette;

    colours::background = juce::Colour (p.background);
    colours::sidebar    = juce::Colour (p.sidebar);
    colours::panel      = juce::Colour (p.panel);
    colours::panelLight = juce::Colour (p.panelLight);
    colours::hover      = juce::Colour (p.hover);
    colours::divider    = juce::Colour (p.divider);
    colours::outline    = juce::Colour (p.outline);
    colours::emptySlot  = juce::Colour (p.emptySlot);
    colours::text       = juce::Colour (p.text);
    colours::textMid    = juce::Colour (p.textMid);
    colours::textDim    = juce::Colour (p.textDim);
    colours::accent     = juce::Colour (p.accent);
    colours::play       = juce::Colour (p.play);
    colours::record     = juce::Colour (p.record);
    colours::warning    = juce::Colour (p.warning);
    colours::selection  = juce::Colour (p.selection);

    if (auto lf = dynamic_cast<SonoraLookAndFeel*> (&juce::LookAndFeel::getDefaultLookAndFeel()))
        lf->refreshColours();

    auto& desktop = juce::Desktop::getInstance();

    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        if (auto c = desktop.getComponent (i))
        {
            if (auto w = dynamic_cast<juce::ResizableWindow*> (c))
                w->setBackgroundColour (colours::background);

            c->sendLookAndFeelChange();
            c->repaint();
        }
    }
}

juce::Colour theme::follow (juce::Colour c)
{
    const auto opaque = c.withAlpha ((juce::uint8) 255).getARGB();
    const auto& now = currentTheme == Kind::light ? lightPalette : darkPalette;
    constexpr size_t n = sizeof (Palette) / sizeof (juce::uint32);
    const auto* nowEntries = reinterpret_cast<const juce::uint32*> (&now);

    for (auto* p : { &darkPalette, &lightPalette })
    {
        const auto* entries = reinterpret_cast<const juce::uint32*> (p);

        for (size_t i = 0; i < n; ++i)
            if (entries[i] == opaque)
                return juce::Colour (nowEntries[i]).withAlpha (c.getAlpha());
    }

    return c;
}

theme::Kind theme::load (juce::PropertySet& props)
{
    return props.getValue ("tema") == "chiaro" ? Kind::light : Kind::dark;
}

void theme::save (juce::PropertySet& props, Kind k)
{
    props.setValue ("tema", name (k));
}

//==============================================================================
SonoraLookAndFeel::SonoraLookAndFeel()
{
    refreshColours();
}

void SonoraLookAndFeel::refreshColours()
{
    setColourScheme ({ colours::background, colours::panel, colours::panelLight,
                       colours::outline, colours::text, colours::accent,
                       colours::background, colours::hover, colours::text });

    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::DocumentWindow::backgroundColourId, colours::background);
    setColour (juce::ListBox::backgroundColourId, colours::sidebar);
    setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, colours::panel);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::highlightColourId, colours::accent.withAlpha (0.35f));
    setColour (juce::CaretComponent::caretColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::panel);
    setColour (juce::TextButton::buttonOnColourId, colours::hover);
    setColour (juce::TextButton::textColourOffId, colours::textMid);
    setColour (juce::TextButton::textColourOnId, colours::text);
    setColour (juce::ToggleButton::textColourId, colours::textMid);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::PopupMenu::backgroundColourId, colours::panelLight);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::background);
    setColour (juce::ComboBox::backgroundColourId, colours::panel);
    setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ScrollBar::thumbColourId, colours::outline);
    setColour (juce::Slider::thumbColourId, colours::text);
    setColour (juce::Slider::trackColourId, colours::accent);
    setColour (juce::Slider::backgroundColourId, colours::panelLight);
    setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, colours::hover);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, colours::panel);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelLight);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::outline);
    setColour (juce::AlertWindow::backgroundColourId, colours::panel);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::TreeView::backgroundColourId, colours::sidebar);
    setColour (juce::TableHeaderComponent::backgroundColourId, colours::panel);
    setColour (juce::TableHeaderComponent::textColourId, colours::textMid);
    setColour (juce::TableHeaderComponent::outlineColourId, colours::divider);

    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, theme::current() == theme::Kind::light ? juce::Colour (0xfffbfbfc) : colours::text);
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, theme::current() == theme::Kind::light ? juce::Colour (0xff2a2c31) : colours::panel);
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, colours::textDim);
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, colours::accent);
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colours::accent.withAlpha (0.3f));
    setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, colours::panel);
    setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, colours::textMid);
}

juce::Typeface::Ptr SonoraLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // I font senza nome esplicito (quelli di default di JUCE) usano Inter.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
         || f.getTypefaceName().isEmpty())
    {
        auto replacement = fonts::ui (f.getHeight(), f.isBold() ? fonts::Weight::semibold : fonts::Weight::regular);

        if (auto tf = replacement.getTypefacePtr())
            return tf;
    }

    return LookAndFeel_V4::getTypefaceForFont (f);
}

void SonoraLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& background,
                                              bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : background;

    if (down)
        colour = colour.brighter (0.12f);
    else if (highlighted)
        colour = colour.brighter (0.06f);

    if (! b.isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    g.setColour (colour);
    g.fillRoundedRectangle (r, metrics::radius);
}

juce::Font SonoraLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return fonts::ui (juce::jmin (13.0f, (float) buttonHeight * 0.5f), fonts::Weight::medium);
}

void SonoraLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat();
    auto box = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.25f);

    g.setColour (b.getToggleState() ? colours::accent : (highlighted ? colours::hover : colours::panelLight));
    g.fillRoundedRectangle (box, 3.0f);

    if (b.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.25f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.45f, box.getBottom() - box.getHeight() * 0.28f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.22f, box.getY() + box.getHeight() * 0.28f);
        g.setColour (colours::background);
        g.strokePath (tick, juce::PathStrokeType (1.6f));
    }

    g.setColour (b.findColour (juce::ToggleButton::textColourId));
    g.setFont (fonts::ui (12.5f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (4.0f), juce::Justification::centredLeft, true);
}

void SonoraLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<int> (w, h).toFloat(), metrics::radius);
}

void SonoraLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& ed)
{
    if (ed.hasKeyboardFocus (true) && ! ed.isReadOnly())
    {
        g.setColour (ed.findColour (juce::TextEditor::focusedOutlineColourId).withAlpha (0.7f));
        g.drawRoundedRectangle (juce::Rectangle<int> (w, h).toFloat().reduced (0.5f), metrics::radius, 1.0f);
    }
}

void SonoraLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h,
                                       bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y, thumbSize, h);

    g.setColour ((over || down) ? colours::hover.brighter (0.2f) : colours::outline);
    g.fillRoundedRectangle (thumb.toFloat().reduced (2.0f), 3.0f);
}

void SonoraLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (colours::panelLight);
    g.setColour (colours::outline);
    g.drawRect (0, 0, w, h);
}

juce::Font SonoraLookAndFeel::getPopupMenuFont()
{
    return fonts::ui (13.0f);
}

juce::Font SonoraLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void SonoraLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
        return;
    }

    const bool vertical = style == juce::Slider::LinearVertical;
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    auto track = vertical ? r.withSizeKeepingCentre (4.0f, r.getHeight()) : r.withSizeKeepingCentre (r.getWidth(), 4.0f);

    g.setColour (colours::panelLight);
    g.fillRoundedRectangle (track, 2.0f);

    auto filled = vertical ? track.withTop (pos) : track.withRight (pos);
    g.setColour (s.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (filled, 2.0f);

    auto thumb = vertical ? juce::Rectangle<float> (18.0f, 8.0f).withCentre ({ track.getCentreX(), pos })
                          : juce::Rectangle<float> (8.0f, 16.0f).withCentre ({ pos, track.getCentreY() });
    g.setColour (s.findColour (juce::Slider::thumbColourId));
    g.fillRoundedRectangle (thumb, 2.5f);
}

void SonoraLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float proportion,
                                          float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const auto angle = startAngle + proportion * (endAngle - startAngle);
    const auto stroke = juce::jmax (2.0f, radius * 0.2f);

    juce::Path background;
    background.addCentredArc (centre.x, centre.y, radius - stroke, radius - stroke, 0.0f, startAngle, endAngle, true);
    g.setColour (s.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (background, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Gli slider bipolari (es. pan) partono dal centro.
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const auto from = bipolar ? (startAngle + endAngle) / 2.0f : startAngle;

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - stroke, radius - stroke, 0.0f,
                         juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Indicatore della posizione
    const auto inner = radius - stroke * 2.6f;
    juce::Point<float> tip (centre.x + inner * std::sin (angle), centre.y - inner * std::cos (angle));
    g.setColour (colours::text);
    g.drawLine ({ centre.getPointOnCircumference (inner * 0.35f, angle), tip }, juce::jmax (1.5f, stroke * 0.7f));
}

} // namespace sonora
