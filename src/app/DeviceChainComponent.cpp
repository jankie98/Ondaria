/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "DeviceChainComponent.h"
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
        return dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr;
    }
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
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 10.0f);

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
        const int knobW = r.getWidth() / knobsPerRow, knobH = 70;

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
};

//==============================================================================
DeviceChainComponent::DeviceChainComponent()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    startTimerHz (10);
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
    if (getUserPlugins() != shownPlugins)
        rebuild();
    else
        for (auto* b : boxes)
            b->refresh();
}

void DeviceChainComponent::rebuild()
{
    boxes.clear();
    shownPlugins = getUserPlugins();

    const auto colour = track != nullptr && ! track->getColour().isTransparent() ? track->getColour() : colours::accent;

    for (auto p : shownPlugins)
        content.addAndMakeVisible (boxes.add (new DeviceBox (p, colour)));

    resized();
    repaint();
}

void DeviceChainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::sidebar);

    if (track != nullptr && ! boxes.isEmpty())
        return;

    auto r = getLocalBounds().reduced (0, 4).withTrimmedLeft (boxes.size() * (boxWidth + 10));
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
    viewport.setBounds (getLocalBounds());

    const int h = juce::jmax (0, getHeight() - viewport.getScrollBarThickness());
    content.setSize (juce::jmax (getWidth(), boxes.size() * (boxWidth + 10)), h);

    for (int i = 0; i < boxes.size(); ++i)
        boxes[i]->setBounds (i * (boxWidth + 10), 0, boxWidth, h);
}

} // namespace sonora
