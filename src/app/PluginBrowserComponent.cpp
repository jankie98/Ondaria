/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "PluginBrowserComponent.h"
#include "SonoraLookAndFeel.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    juce::PluginDescription makeBuiltIn (const juce::String& xmlType, const juce::String& name,
                                         const juce::String& category, bool isInstrument)
    {
        juce::PluginDescription d;
        d.name = name;
        d.pluginFormatName = PluginBrowserComponent::builtInFormat;
        d.fileOrIdentifier = xmlType;
        d.category = category;
        d.isInstrument = isInstrument;
        d.manufacturerName = "Tracktion Engine";
        return d;
    }
}

PluginBrowserComponent::PluginBrowserComponent (te::Engine& e) : engine (e)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (title);

    search.setTextToShowWhenEmpty ("Cerca...", colours::textDim);
    search.onTextChange = [this] { rebuildList(); };
    addAndMakeVisible (search);

    manageButton.onClick = [this] { openPluginManager(); };
    addAndMakeVisible (manageButton);

    list.setModel (this);
    list.setRowHeight (22);
    addAndMakeVisible (list);

    engine.getPluginManager().knownPluginList.addChangeListener (this);
    rebuildList();
}

PluginBrowserComponent::~PluginBrowserComponent()
{
    engine.getPluginManager().knownPluginList.removeChangeListener (this);
}

void PluginBrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
}

void PluginBrowserComponent::resized()
{
    auto r = getLocalBounds().reduced (6);
    title.setBounds (r.removeFromTop (24));
    search.setBounds (r.removeFromTop (26));
    r.removeFromTop (4);
    manageButton.setBounds (r.removeFromBottom (28));
    r.removeFromBottom (4);
    list.setBounds (r);
}

void PluginBrowserComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildList();
}

void PluginBrowserComponent::rebuildList()
{
    allItems.clearQuick();

    allItems.add (makeBuiltIn (te::FourOscPlugin::xmlTypeName, "4OSC (sintetizzatore)", "Instrument", true));
    allItems.add (makeBuiltIn (te::SamplerPlugin::xmlTypeName, "Sampler", "Instrument", true));
    allItems.add (makeBuiltIn (te::EqualiserPlugin::xmlTypeName, "EQ", "Effect", false));
    allItems.add (makeBuiltIn (te::CompressorPlugin::xmlTypeName, "Compressore", "Effect", false));
    allItems.add (makeBuiltIn (te::ReverbPlugin::xmlTypeName, "Riverbero", "Effect", false));
    allItems.add (makeBuiltIn (te::DelayPlugin::xmlTypeName, "Delay", "Effect", false));
    allItems.add (makeBuiltIn (te::ChorusPlugin::xmlTypeName, "Chorus", "Effect", false));
    allItems.add (makeBuiltIn (te::PhaserPlugin::xmlTypeName, "Phaser", "Effect", false));
    allItems.add (makeBuiltIn (te::LowPassPlugin::xmlTypeName, "Filtro", "Effect", false));

    auto types = engine.getPluginManager().knownPluginList.getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b)
    {
        return a.name.compareIgnoreCase (b.name) < 0;
    });

    allItems.addArray (types);

    const auto filter = search.getText().trim();
    visibleItems.clearQuick();

    for (auto& d : allItems)
        if (filter.isEmpty() || d.name.containsIgnoreCase (filter)
             || d.manufacturerName.containsIgnoreCase (filter) || d.category.containsIgnoreCase (filter)
             || d.pluginFormatName.containsIgnoreCase (filter))
            visibleItems.add (d);

    list.updateContent();
    list.repaint();
}

int PluginBrowserComponent::getNumRows()
{
    return visibleItems.size();
}

void PluginBrowserComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, visibleItems.size()))
        return;

    const auto& d = visibleItems.getReference (row);

    if (selected)
        g.fillAll (colours::selection);

    auto r = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);

    g.setColour (colours::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (d.pluginFormatName, r.removeFromRight (64), juce::Justification::centredRight);

    g.setColour (d.isInstrument ? colours::accent : colours::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (d.name, r, juce::Justification::centredLeft, true);
}

juce::String PluginBrowserComponent::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, visibleItems.size()))
        return {};

    const auto& d = visibleItems.getReference (row);
    return d.name + " - " + d.manufacturerName + " (" + d.pluginFormatName + ", " + d.category + ")";
}

void PluginBrowserComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    returnKeyPressed (row);
}

void PluginBrowserComponent::returnKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, visibleItems.size()) && onPluginChosen)
        onPluginChosen (visibleItems.getReference (row));
}

void PluginBrowserComponent::openPluginManager()
{
    auto& pm = engine.getPluginManager();

    auto deadMansPedal = engine.getPropertyStorage().getAppPrefsFolder().getChildFile ("plugin-scan-in-corso.txt");

    auto content = std::make_unique<juce::PluginListComponent> (pm.pluginFormatManager, pm.knownPluginList,
                                                                deadMansPedal, nullptr, true);
    content->setSize (760, 520);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content.release());
    options.dialogTitle = "Plugin: scansione e lista di esclusione";
    options.dialogBackgroundColour = colours::background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    managerWindow.reset (options.create());
    managerWindow->setVisible (true);
}

} // namespace sonora
