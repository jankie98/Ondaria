/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "PluginBrowserComponent.h"
#include "Settings.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    const juce::Colour instrumentColour { 0xfff2a93b }, effectColour { 0xff4ade80 },
                       pluginColour { 0xff7c9cff }, sampleColour { 0xffc084fc };

    juce::PluginDescription makeBuiltIn (const juce::String& xmlType, const juce::String& name,
                                         const juce::String& category, bool isInstrument)
    {
        juce::PluginDescription d;
        d.name = name;
        d.pluginFormatName = PluginBrowserComponent::builtInFormat;
        d.fileOrIdentifier = xmlType;
        d.category = category;
        d.isInstrument = isInstrument;
        d.manufacturerName = "Ondaria";
        return d;
    }

    constexpr const char* sampleFolderKey = "cartellaCampioni";

    std::unique_ptr<juce::PropertiesFile> openSettings (te::Engine& e)    { return openUiSettings (e); }
}

PluginBrowserComponent::PluginBrowserComponent (te::Engine& e) : engine (e)
{
    search.setTextToShowWhenEmpty ("Cerca nel browser", colours::textDim);
    search.setFont (fonts::ui (13.0f));
    search.setIndents (30, 8);
    search.onTextChange = [this] { rebuildRows(); };
    search.onReturnKey = [this] { returnKeyPressed (juce::jmax (0, list.getSelectedRow())); };
    addAndMakeVisible (search);

    const std::pair<const char*, Filter> filters[] = { { "Tutto", Filter::all }, { "Strumenti", Filter::instruments },
                                                       { "Effetti", Filter::effects }, { "Plugin", Filter::plugins },
                                                       { "Campioni", Filter::samples } };

    for (auto [label, f] : filters)
    {
        auto* chip = chips.add (new FlatButton (label, 11.5f));
        chip->setBaseColour (colours::panelLight);
        chip->setOnColours (colours::accent, colours::ink);
        chip->onClick = [this, f = f] { setFilter (f); };
        addAndMakeVisible (chip);
    }

    manageButton.setBaseColour (colours::panel);
    manageButton.onClick = [this] { openPluginManager(); };
    addAndMakeVisible (manageButton);

    folderButton.setBaseColour (colours::panel);
    folderButton.onClick = [this] { chooseSampleFolder(); };
    addChildComponent (folderButton);

    list.setModel (this);
    list.setRowHeight (28);
    addAndMakeVisible (list);
    lookAndFeelChanged();

    sampleFolder = juce::File (openSettings (engine)->getValue (sampleFolderKey));

    if (! sampleFolder.isDirectory())
        sampleFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    engine.getPluginManager().knownPluginList.addChangeListener (this);
    rebuildDevices();
    setFilter (Filter::all);
}

PluginBrowserComponent::~PluginBrowserComponent()
{
    engine.getPluginManager().knownPluginList.removeChangeListener (this);
}

void PluginBrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::sidebar);
    g.setColour (colours::divider);
    g.fillRect (getLocalBounds().removeFromRight (1));
}

void PluginBrowserComponent::paintOverChildren (juce::Graphics& g)
{
    icons::draw (g, icons::search(), search.getBounds().removeFromLeft (30).withSizeKeepingCentre (13, 13).toFloat(),
                 colours::textDim);
}

void PluginBrowserComponent::resized()
{
    auto r = getLocalBounds().reduced (12, 14);
    search.setBounds (r.removeFromTop (32));
    r.removeFromTop (10);

    // Etichette su piu' righe se non ci stanno.
    int x = r.getX(), y = r.getY();

    for (auto* chip : chips)
    {
        const int w = (int) juce::GlyphArrangement::getStringWidth (fonts::ui (11.5f), chip->getButtonText()) + 22;

        if (x + w > r.getRight())
        {
            x = r.getX();
            y += 28;
        }

        chip->setBounds (x, y, w, 24);
        x += w + 6;
    }

    r.setTop (y + 24 + 10);
    auto bottom = r.removeFromBottom (30);
    manageButton.setBounds (bottom);
    folderButton.setBounds (bottom);
    r.removeFromBottom (8);
    list.setBounds (r);
}

void PluginBrowserComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildDevices();
    rebuildRows();
}

void PluginBrowserComponent::rebuildDevices()
{
    allItems.clearQuick();

    allItems.add (makeBuiltIn (te::FourOscPlugin::xmlTypeName, "Sintetizzatore 4OSC", "Instrument", true));
    allItems.add (makeBuiltIn (te::SamplerPlugin::xmlTypeName, "Campionatore", "Instrument", true));
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
}

void PluginBrowserComponent::setFilter (Filter f)
{
    filter = f;

    for (int i = 0; i < chips.size(); ++i)
        chips[i]->setToggleState (i == (int) f, juce::dontSendNotification);

    manageButton.setVisible (f != Filter::samples);
    folderButton.setVisible (f == Filter::samples);
    rebuildRows();
}

void PluginBrowserComponent::rebuildRows()
{
    rows.clear();
    const auto query = search.getText().trim();

    auto matches = [&query] (const juce::String& a, const juce::String& b)
    {
        return query.isEmpty() || a.containsIgnoreCase (query) || b.containsIgnoreCase (query);
    };

    if (filter != Filter::samples)
    {
        for (auto& d : allItems)
        {
            const bool builtIn = d.pluginFormatName == builtInFormat;

            if ((filter == Filter::instruments && ! d.isInstrument)
                 || (filter == Filter::effects && d.isInstrument)
                 || (filter == Filter::plugins && builtIn))
                continue;

            if (! matches (d.name, d.manufacturerName + " " + d.pluginFormatName + " " + d.category))
                continue;

            rows.push_back ({ d.name, d.pluginFormatName,
                              builtIn ? (d.isInstrument ? instrumentColour : effectColour) : pluginColour, d, {} });
        }
    }

    if (filter == Filter::samples || (filter == Filter::all && query.isNotEmpty()))
    {
        // In "Tutto" i campioni compaiono solo cercando, per non affollare la lista.
        auto files = sampleFolder.findChildFiles (juce::File::findFiles, true,
                                                  juce::String (te::soundFileExtensions) + ";" + te::midiFileExtensions);
        files.sort();

        for (auto& f : files)
        {
            if (rows.size() > 2000)
                break;

            if (matches (f.getFileName(), {}))
                rows.push_back ({ f.getFileName(), f.getFileExtension().substring (1).toUpperCase(), sampleColour, {}, f });
        }
    }

    list.updateContent();
    list.repaint();
}

void PluginBrowserComponent::chooseSampleFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Cartella dei campioni", sampleFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().isDirectory())
        {
            sampleFolder = fc.getResult();
            auto settings = openSettings (engine);
            settings->setValue (sampleFolderKey, sampleFolder.getFullPathName());
            settings->saveIfNeeded();
            rebuildRows();
        }
    });
}

int PluginBrowserComponent::getNumRows()
{
    return (int) rows.size();
}

void PluginBrowserComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return;

    const auto& item = rows[(size_t) row];
    auto r = juce::Rectangle<int> (0, 0, width, height);

    if (selected)
    {
        g.setColour (colours::hover);
        g.fillRoundedRectangle (r.toFloat().reduced (0.0f, 1.0f), metrics::radius);
    }

    r.reduce (8, 0);
    g.setColour (item.dot);
    g.fillEllipse (r.removeFromLeft (6).withSizeKeepingCentre (6, 6).toFloat());
    r.removeFromLeft (10);

    g.setColour (colours::textDim);
    g.setFont (fonts::mono (10.0f));
    g.drawText (item.detail, r.removeFromRight (64), juce::Justification::centredRight, true);

    g.setColour (colours::text);
    g.setFont (fonts::ui (12.5f));
    g.drawText (item.name, r, juce::Justification::centredLeft, true);
}

juce::String PluginBrowserComponent::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) rows.size()))
        return {};

    const auto& item = rows[(size_t) row];

    if (item.file != juce::File())
        return item.file.getFullPathName() + "\nTrascinalo su uno slot per creare una clip";

    const auto& d = item.device;
    return d.name + " - " + d.manufacturerName + " (" + d.pluginFormatName + ")\nDoppio clic per aggiungerlo alla traccia";
}

juce::var PluginBrowserComponent::getDragSourceDescription (const juce::SparseSet<int>& selected)
{
    if (selected.isEmpty())
        return {};

    const int row = selected[0];

    if (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].file != juce::File())
        return "file:" + rows[(size_t) row].file.getFullPathName();

    return {};
}

void PluginBrowserComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    returnKeyPressed (row);
}

void PluginBrowserComponent::returnKeyPressed (int row)
{
    if (juce::isPositiveAndBelow (row, (int) rows.size()) && rows[(size_t) row].file == juce::File() && onPluginChosen)
        onPluginChosen (rows[(size_t) row].device);
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
