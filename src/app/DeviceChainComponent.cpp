/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "DeviceChainComponent.h"
#include "DeviceVisualiser.h"
#include "FreezeUI.h"
#include "Widgets.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    constexpr int boxWidth = 244;
    constexpr int knobsPerRow = 4;

    bool isMixerPlugin (te::Plugin* p)
    {
        return dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr
            || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr
            || dynamic_cast<te::FreezePointPlugin*> (p) != nullptr;
    }

    constexpr int frozenBannerHeight = 34;
}

//==============================================================================
/** Una manopola collegata a un parametro automatizzabile del device. */
class ParameterKnob final : public juce::Component
{
public:
    explicit ParameterKnob (te::AutomatableParameter::Ptr p) : param (std::move (p))
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRange (0.0, 1.0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        slider.setPopupDisplayEnabled (false, false, nullptr);
        slider.onDragStart = [this] { param->parameterChangeGestureBegin(); };
        slider.onDragEnd = [this] { param->parameterChangeGestureEnd(); };
        slider.onValueChange = [this] { param->setNormalisedParameter ((float) slider.getValue(), juce::sendNotification); };
        slider.setTooltip (param->getParameterName());
        addAndMakeVisible (slider);
        refresh();
    }

    void setColour (juce::Colour c)     { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    void refresh()
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (param->getCurrentNormalisedValue(), juce::dontSendNotification);

        auto text = param->getCurrentValueAsStringWithLabel();

        if (text != valueText)
        {
            valueText = text;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().withTrimmedTop (40);
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (10.5f));
        g.drawText (param->getParameterShortName (10), r.removeFromTop (14), juce::Justification::centred, true);
        g.setColour (colours::textMid);
        g.setFont (fonts::mono (10.0f));
        g.drawText (valueText, r.removeFromTop (14), juce::Justification::centred, true);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().removeFromTop (40).withSizeKeepingCentre (40, 40));
    }

private:
    te::AutomatableParameter::Ptr param;
    juce::Slider slider;
    juce::String valueText;
};

//==============================================================================
class DeviceChainComponent::DeviceBox final : public juce::Component
{
public:
    DeviceBox (te::Plugin::Ptr p, juce::Colour c) : plugin (std::move (p)), colour (c)
    {
        powerButton.setActiveColour (colour);
        powerButton.setIconColour (colours::textDim);
        powerButton.onClick = [this]
        {
            plugin->edit.getUndoManager().beginNewTransaction ("Attiva/disattiva device");
            plugin->setEnabled (! plugin->isEnabled());
            refresh();
        };

        openButton.setBaseColour (colours::panelLight);
        openButton.onClick = [this] { plugin->showWindowExplicitly(); };

        removeButton.setIconColour (colours::textDim);
        removeButton.onClick = [this]
        {
            juce::MessageManager::callAsync ([p = plugin]
            {
                p->edit.getUndoManager().beginNewTransaction ("Rimuovi device");
                p->deleteFromParent();
            });
        };

        for (auto* b : std::initializer_list<juce::Component*> { &powerButton, &openButton, &removeButton })
            addAndMakeVisible (b);

        if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
        {
            format = external->desc.pluginFormatName;

            if (external->getAudioPluginInstance() == nullptr)
                error = "Non caricato: " + external->getLoadError();
        }
        else
        {
            format = "Integrato";
        }

        if ((visualiser = DeviceVisualiser::create (*plugin, colour)))
            addAndMakeVisible (*visualiser);

        // Le prime otto manopole: per tutti i parametri c'e' la finestra del device.
        auto params = plugin->getAutomatableParameters();

        for (int i = 0; i < juce::jmin (8, params.size()); ++i)
        {
            auto* k = knobs.add (new ParameterKnob (params[i]));
            k->setColour (colour);
            addAndMakeVisible (k);
        }

        refresh();
    }

    void refresh()
    {
        powerButton.setToggleState (plugin->isEnabled(), juce::dontSendNotification);

        for (auto* k : knobs)
        {
            k->refresh();
            k->setAlpha (plugin->isEnabled() ? 1.0f : 0.45f);
        }

        if (visualiser != nullptr)
        {
            visualiser->refresh();
            visualiser->setAlpha (plugin->isEnabled() ? 1.0f : 0.45f);
        }
    }

    int getPreferredWidth() const
    {
        if (visualiser == nullptr)
            return boxWidth;

        // Col disegno, la scheda e' larga quanto le manopole che mostra davvero
        const int columns = juce::jlimit (2, knobsPerRow, knobs.size());
        return 24 + DeviceVisualiser::preferredWidth + 10 + columns * (boxWidth - 24) / knobsPerRow;
    }

    juce::Colour getColour() const      { return colour; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 10.0f);

        // Fascia col colore della traccia in alto
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (getLocalBounds().removeFromTop (3));
            g.setColour (plugin->isEnabled() ? colour : colour.withSaturation (0.15f));
            g.fillRoundedRectangle (r, 10.0f);
        }

        auto header = getLocalBounds().reduced (12, 10).removeFromTop (24);
        header.removeFromLeft (26);
        header.removeFromRight (26 + 50 + 6);

        auto badgeWidth = (int) juce::GlyphArrangement::getStringWidth (fonts::mono (10.0f), format) + 12;
        auto badge = header.removeFromRight (badgeWidth).withSizeKeepingCentre (badgeWidth, 18);
        g.setColour (colours::panelLight);
        g.fillRoundedRectangle (badge.toFloat(), 4.0f);
        g.setColour (colours::textDim);
        g.setFont (fonts::mono (10.0f));
        g.drawText (format, badge, juce::Justification::centred);

        g.setColour (plugin->isEnabled() ? colours::text : colours::textDim);
        g.setFont (fonts::ui (13.0f, fonts::Weight::medium));
        g.drawText (plugin->getName(), header.withTrimmedRight (6), juce::Justification::centredLeft, true);

        if (error.isNotEmpty())
        {
            g.setColour (colours::record);
            g.setFont (fonts::ui (11.5f));
            g.drawFittedText (error, getLocalBounds().reduced (12).withTrimmedTop (36), juce::Justification::topLeft, 4);
        }
        else if (knobs.isEmpty())
        {
            g.setColour (colours::textDim);
            g.setFont (fonts::ui (11.5f));
            g.drawFittedText ("Apri il device per vederne i controlli", getLocalBounds().reduced (12).withTrimmedTop (36),
                              juce::Justification::topLeft, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        auto header = r.removeFromTop (24);
        powerButton.setBounds (header.removeFromLeft (24));
        removeButton.setBounds (header.removeFromRight (24));
        header.removeFromRight (4);
        openButton.setBounds (header.removeFromRight (50).withSizeKeepingCentre (50, 22));

        r.removeFromTop (10);

        if (visualiser != nullptr)
        {
            visualiser->setBounds (r.removeFromLeft (DeviceVisualiser::preferredWidth).withTrimmedBottom (2));
            r.removeFromLeft (10);
        }

        const int knobW = (boxWidth - 24) / knobsPerRow, knobH = 70;

        for (int i = 0; i < knobs.size(); ++i)
        {
            const int row = i / knobsPerRow, col = i % knobsPerRow;
            auto area = juce::Rectangle<int> (r.getX() + col * knobW, r.getY() + row * (knobH + 4), knobW, knobH);
            knobs[i]->setBounds (area);
            knobs[i]->setVisible (area.getBottom() <= getHeight() - 6);
        }
    }

    te::Plugin::Ptr plugin;

private:
    juce::Colour colour;
    juce::String format, error;
    IconButton powerButton { "Attiva/disattiva", icons::power() };
    FlatButton openButton { "Apri", 11.5f };
    IconButton removeButton { "Rimuovi", icons::close() };
    juce::OwnedArray<ParameterKnob> knobs;
    std::unique_ptr<DeviceVisualiser> visualiser;
};

//==============================================================================
DeviceChainComponent::DeviceChainComponent()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addChildComponent (unfreezeButton);
    unfreezeButton.onClick = [this]
    {
        if (track != nullptr)
            freeze::unfreeze (*track);

        rebuild();
    };
    startTimerHz (30);
}

DeviceChainComponent::~DeviceChainComponent()
{
    boxes.clear();
}

void DeviceChainComponent::setTrack (te::AudioTrack* t)
{
    track = t;
    rebuild();
}

te::Plugin::Array DeviceChainComponent::getUserPlugins() const
{
    te::Plugin::Array result;

    if (track != nullptr)
        for (auto p : track->pluginList.getPlugins())
            if (! isMixerPlugin (p))
                result.add (p);

    return result;
}

void DeviceChainComponent::timerCallback()
{
    if (getUserPlugins() != shownPlugins || trackColour() != shownColour || isTrackFrozen() != shownFrozen)
        rebuild();
    else
        for (auto* b : boxes)
            b->refresh();
}

void DeviceChainComponent::rebuild()
{
    boxes.clear();
    shownPlugins = getUserPlugins();

    shownColour = trackColour();
    shownFrozen = isTrackFrozen();
    unfreezeButton.setVisible (shownFrozen);

    for (auto p : shownPlugins)
    {
        auto box = boxes.add (new DeviceBox (p, shownColour));
        content.addAndMakeVisible (box);

        // I device congelati sono fermi: si vedono ma non si toccano
        box->setEnabled (! p->isFrozen());
        box->setAlpha (p->isFrozen() ? 0.4f : 1.0f);
    }

    resized();
    repaint();
}

bool DeviceChainComponent::isTrackFrozen() const
{
    return track != nullptr && freeze::isFrozen (*track);
}

juce::Colour DeviceChainComponent::trackColour() const
{
    return track != nullptr && ! track->getColour().isTransparent() ? track->getColour() : colours::accent;
}

void DeviceChainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::sidebar);

    if (shownFrozen)
    {
        auto banner = getLocalBounds().removeFromTop (frozenBannerHeight).reduced (0, 4).toFloat();
        g.setColour (freezeui::iceColour().withAlpha (0.1f));
        g.fillRoundedRectangle (banner, 6.0f);
        freezeui::drawSnowflake (g, banner.removeFromLeft (30.0f).withSizeKeepingCentre (14.0f, 14.0f), freezeui::iceColour());
        g.setColour (colours::textMid);
        g.setFont (fonts::ui (12.0f));
        g.drawText (juce::String::fromUTF8 ("Traccia congelata: suona dal file e i device prima del fader sono fermi. "
                                            "Scongelala per modificarli."),
                    banner.withTrimmedRight (110.0f), juce::Justification::centredLeft, true);
    }

    if (track != nullptr && ! boxes.isEmpty())
        return;

    auto r = getLocalBounds().withTrimmedTop (shownFrozen ? frozenBannerHeight : 0).reduced (0, 4);
    auto hint = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 20, 420), juce::jmin (r.getHeight() - 8, 120));
    g.setColour (colours::outline);
    g.drawRoundedRectangle (hint.toFloat(), 10.0f, 1.0f);   // tratteggio non necessario: bordo sottile
    g.setColour (colours::textDim);
    g.setFont (fonts::ui (12.5f));
    g.drawFittedText (track == nullptr ? "Seleziona una traccia"
                                       : "Fai doppio clic su un device o un plugin nel browser per aggiungerlo a \""
                                            + track->getName() + "\"",
                      hint.reduced (16), juce::Justification::centred, 3);
}

void DeviceChainComponent::resized()
{
    auto area = getLocalBounds();

    if (shownFrozen)
    {
        auto banner = area.removeFromTop (frozenBannerHeight).reduced (0, 4);
        unfreezeButton.setBounds (banner.removeFromRight (96).reduced (4, 3));
        area.removeFromTop (4);
    }

    viewport.setBounds (area);

    const int h = juce::jmax (0, viewport.getHeight() - viewport.getScrollBarThickness());
    int x = 0;

    for (auto* b : boxes)
    {
        b->setBounds (x, 0, b->getPreferredWidth(), h);
        x += b->getPreferredWidth() + 10;
    }

    content.setSize (juce::jmax (getWidth(), x), h);
}

} // namespace sonora
