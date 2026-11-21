/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MainComponent.h"
#include "SonoraLookAndFeel.h"

namespace te = tracktion;
using namespace std::literals;

namespace sonora
{

static constexpr const char* keyboardDeviceName = "Tastiera Sonora";

MainComponent::MainComponent (te::Engine& e)
    : engine (e), browser (e)
{
    audioButton.onClick = [this] { showAudioSettings(); };
    playButton.onClick = [this]
    {
        if (edit != nullptr)
            edit->getTransport().play (false);
    };
    stopButton.onClick = [this]
    {
        if (edit != nullptr)
        {
            auto& transport = edit->getTransport();

            if (transport.isPlaying())
                transport.stop (false, false);
            else
                transport.setPosition (0s);
        }
    };
    addTrackButton.onClick = [this]
    {
        if (edit != nullptr)
        {
            edit->ensureNumberOfAudioTracks (te::getAudioTracks (*edit).size() + 1);
            trackList.updateContent();
            trackList.selectRow (te::getAudioTracks (*edit).size() - 1);
        }
    };

    tempoSlider.setRange (20.0, 999.0, 0.1);
    tempoSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 24);
    tempoSlider.onValueChange = [this]
    {
        if (edit != nullptr)
            edit->tempoSequence.getTempos()[0]->setBpm (tempoSlider.getValue());
    };

    cpuLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, colours::textDim);

    browser.onPluginChosen = [this] (const juce::PluginDescription& d) { insertDevice (d); };

    trackList.setModel (this);
    trackList.setRowHeight (34);

    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (48);
    keyboardState.addListener (this);

    for (auto* c : std::initializer_list<juce::Component*> { &audioButton, &playButton, &stopButton, &addTrackButton,
                                                             &tempoSlider, &tempoLabel, &cpuLabel, &statusLabel,
                                                             &browser, &trackList, &deviceChain, &keyboard })
        addAndMakeVisible (c);

    // Tastiera virtuale: permette di suonare gli strumenti anche senza controller.
    auto& dm = engine.getDeviceManager();

    if (dm.createVirtualMidiDevice (keyboardDeviceName).wasOk())
        dm.dispatchPendingUpdates();

    for (auto& m : dm.getMidiInDevices())
    {
        m->setEnabled (true);
        m->setMonitorMode (te::InputDevice::MonitorMode::on);

        if (m->getName() == keyboardDeviceName)
            keyboardDevice = m;
    }

    createOrLoadEdit();

    setSize (1280, 800);
    startTimerHz (10);
}

MainComponent::~MainComponent()
{
    keyboardState.removeListener (this);
    save();

    if (edit != nullptr)
        edit->getTransport().removeChangeListener (this);
}

void MainComponent::createOrLoadEdit()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Sonora");
    folder.createDirectory();
    auto file = folder.getChildFile ("Senza titolo.tracktionedit");

    if (file.existsAsFile())
        edit = te::loadEditFromFile (engine, file);
    else
        edit = te::createEmptyEdit (engine, file);

    edit->playInStopEnabled = true;
    edit->ensureNumberOfAudioTracks (4);
    edit->getTransport().addChangeListener (this);
    edit->getTransport().ensureContextAllocated();

    tempoSlider.setValue (edit->tempoSequence.getTempos()[0]->getBpm(), juce::dontSendNotification);

    trackList.updateContent();
    trackList.selectRow (0);
    statusLabel.setText ("Progetto: " + file.getFullPathName(), juce::dontSendNotification);
    updateTransportButtons();
}

void MainComponent::save()
{
    if (edit != nullptr)
        te::EditFileOperations (*edit).save (true, true, false);
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    g.setColour (colours::panel);
    g.fillRect (getLocalBounds().removeFromTop (44));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();

    auto top = r.removeFromTop (44).reduced (8, 8);
    audioButton.setBounds (top.removeFromLeft (80));
    top.removeFromLeft (16);
    playButton.setBounds (top.removeFromLeft (64));
    top.removeFromLeft (4);
    stopButton.setBounds (top.removeFromLeft (64));
    top.removeFromLeft (16);
    tempoSlider.setBounds (top.removeFromLeft (130));
    tempoLabel.setBounds (top.removeFromLeft (40));
    cpuLabel.setBounds (top.removeFromRight (120));

    statusLabel.setBounds (r.removeFromBottom (22).reduced (8, 0));
    keyboard.setBounds (r.removeFromBottom (64));
    keyboard.setKeyWidth (juce::jmax (8.0f, (float) keyboard.getWidth() / 50.0f));

    browser.setBounds (r.removeFromLeft (280));
    deviceChain.setBounds (r.removeFromBottom (190));

    auto tracksArea = r.reduced (8);
    addTrackButton.setBounds (tracksArea.removeFromBottom (28).removeFromLeft (110));
    tracksArea.removeFromBottom (6);
    trackList.setBounds (tracksArea);
}

//==============================================================================
int MainComponent::getNumRows()
{
    return edit != nullptr ? te::getAudioTracks (*edit).size() : 0;
}

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (edit == nullptr)
        return;

    auto track = te::getAudioTracks (*edit)[row];

    if (track == nullptr)
        return;

    g.fillAll (selected ? colours::selection : colours::panel);
    g.setColour (track->getColour().isTransparent() ? colours::accent : track->getColour());
    g.fillRect (0, 2, 4, height - 4);

    int numDevices = 0;

    for (auto p : track->pluginList.getPlugins())
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) == nullptr && dynamic_cast<te::LevelMeterPlugin*> (p) == nullptr)
            ++numDevices;

    g.setColour (colours::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (track->getName(), 14, 0, width - 140, height, juce::Justification::centredLeft, true);
    g.setColour (colours::textDim);
    g.drawText (juce::String (numDevices) + (numDevices == 1 ? " device" : " device"),
                width - 120, 0, 110, height, juce::Justification::centredRight);
}

void MainComponent::selectedRowsChanged (int)
{
    auto track = getSelectedTrack();
    deviceChain.setTrack (track);
    routeKeyboardTo (track);
}

te::AudioTrack* MainComponent::getSelectedTrack() const
{
    if (edit == nullptr)
        return nullptr;

    return te::getAudioTracks (*edit)[trackList.getSelectedRow()];
}

void MainComponent::routeKeyboardTo (te::AudioTrack* track)
{
    if (edit == nullptr || track == nullptr)
        return;

    for (auto* instance : edit->getAllInputDevices())
    {
        if (instance->getInputDevice().getDeviceType() != te::InputDevice::physicalMidiDevice
             && instance->getInputDevice().getDeviceType() != te::InputDevice::virtualMidiDevice)
            continue;

        for (auto target : instance->getTargets())
            if (target != track->itemID)
                [[maybe_unused]] auto res = instance->removeTarget (target, nullptr);

        [[maybe_unused]] auto res = instance->setTarget (track->itemID, false, nullptr, 0);
    }

    edit->restartPlayback();
}

//==============================================================================
void MainComponent::insertDevice (const juce::PluginDescription& desc)
{
    auto track = getSelectedTrack();

    if (track == nullptr)
        return;

    te::Plugin::Ptr plugin;

    if (desc.pluginFormatName == PluginBrowserComponent::builtInFormat)
        plugin = edit->getPluginCache().createNewPlugin (desc.fileOrIdentifier, {});
    else
        plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);

    if (plugin == nullptr)
    {
        statusLabel.setText ("Impossibile creare: " + desc.name, juce::dontSendNotification);
        return;
    }

    // Prima del volume/pan e del misuratore, che restano in fondo alla catena.
    int index = 0;

    for (auto p : track->pluginList.getPlugins())
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr)
            break;

        ++index;
    }

    track->pluginList.insertPlugin (plugin, index, nullptr);

    if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
        external != nullptr && external->getAudioPluginInstance() == nullptr)
        statusLabel.setText ("Errore nel caricare " + desc.name + ": " + external->getLoadError(), juce::dontSendNotification);
    else
        statusLabel.setText ("Aggiunto " + desc.name + " (" + desc.pluginFormatName + ") a " + track->getName(),
                             juce::dontSendNotification);

    trackList.repaint();
}

//==============================================================================
void MainComponent::handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (keyboardDevice != nullptr)
        keyboardDevice->handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOn (channel, note, velocity));
}

void MainComponent::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (keyboardDevice != nullptr)
        keyboardDevice->handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOff (channel, note, velocity));
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateTransportButtons();
}

void MainComponent::updateTransportButtons()
{
    const bool playing = edit != nullptr && edit->getTransport().isPlaying();
    playButton.setToggleState (playing, juce::dontSendNotification);
}

void MainComponent::timerCallback()
{
    const auto cpu = engine.getDeviceManager().getCpuUsage();
    cpuLabel.setText ("CPU " + juce::String (juce::roundToInt (cpu * 100.0)) + "%", juce::dontSendNotification);
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (engine.getDeviceManager().deviceManager,
                                                                          0, 512, 1, 512, true, true, true, false);
    selector->setSize (560, 460);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Impostazioni audio e MIDI";
    options.dialogBackgroundColour = colours::background;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

} // namespace sonora
