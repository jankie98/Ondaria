/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "DeviceChainComponent.h"
#include "SonoraLookAndFeel.h"

namespace te = tracktion;

namespace sonora
{

//==============================================================================
class DeviceChainComponent::DeviceBox final : public juce::Component
{
public:
    explicit DeviceBox (te::Plugin::Ptr p) : plugin (std::move (p))
    {
        openButton.onClick = [this] { plugin->showWindowExplicitly(); };
        enableButton.setClickingTogglesState (true);
        enableButton.setToggleState (plugin->isEnabled(), juce::dontSendNotification);
        enableButton.onClick = [this] { plugin->setEnabled (enableButton.getToggleState()); };
        removeButton.onClick = [this]
        {
            juce::MessageManager::callAsync ([p = plugin] { p->deleteFromParent(); });
        };

        for (auto* b : { &openButton, &enableButton, &removeButton })
            addAndMakeVisible (b);

        if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
            subtitle = external->desc.pluginFormatName + " - " + external->desc.manufacturerName;
        else
            subtitle = "Integrato";

        if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get()); external != nullptr
             && external->getAudioPluginInstance() == nullptr)
            subtitle = "Non caricato: " + external->getLoadError();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (colours::panelLight);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (plugin->isEnabled() ? colours::accent : colours::outline);
        g.drawRoundedRectangle (r, 6.0f, 1.5f);

        auto text = getLocalBounds().reduced (10, 8);
        g.setColour (colours::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (plugin->getName(), text.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (colours::textDim);
        g.setFont (juce::FontOptions (11.5f));
        g.drawFittedText (subtitle, text.removeFromTop (32), juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10).removeFromBottom (26);
        const int w = r.getWidth() / 3;
        enableButton.setBounds (r.removeFromLeft (w).reduced (2, 0));
        openButton.setBounds (r.removeFromLeft (w).reduced (2, 0));
        removeButton.setBounds (r.reduced (2, 0));
    }

    te::Plugin::Ptr plugin;

private:
    juce::TextButton openButton { "Apri" }, enableButton { "On" }, removeButton { "X" };
    juce::String subtitle;
};

//==============================================================================
DeviceChainComponent::DeviceChainComponent()
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    startTimerHz (4);
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

    if (track == nullptr)
        return result;

    for (auto p : track->pluginList.getPlugins())
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr
             && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr)
            result.add (p);

    return result;
}

void DeviceChainComponent::timerCallback()
{
    if (getUserPlugins() != shownPlugins)
        rebuild();
    else
        for (auto* b : boxes)
            b->repaint();
}

void DeviceChainComponent::rebuild()
{
    boxes.clear();
    shownPlugins = getUserPlugins();

    for (auto p : shownPlugins)
        content.addAndMakeVisible (boxes.add (new DeviceBox (p)));

    resized();
    repaint();
}

void DeviceChainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    if (track == nullptr)
    {
        g.setColour (colours::textDim);
        g.drawText ("Seleziona una traccia", getLocalBounds(), juce::Justification::centred);
    }
    else if (boxes.isEmpty())
    {
        g.setColour (colours::textDim);
        g.drawText ("Fai doppio clic su un device nel browser per aggiungerlo a \"" + track->getName() + "\"",
                    getLocalBounds(), juce::Justification::centred);
    }
}

void DeviceChainComponent::resized()
{
    viewport.setBounds (getLocalBounds());

    const int boxWidth = 210, h = juce::jmax (0, getHeight() - viewport.getScrollBarThickness());
    content.setSize (juce::jmax (getWidth(), boxes.size() * boxWidth + 8), h);

    for (int i = 0; i < boxes.size(); ++i)
        boxes[i]->setBounds (4 + i * boxWidth, 4, boxWidth - 6, juce::jmax (0, h - 8));
}

} // namespace sonora
