/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MainComponent.h"
#include "Theme.h"
#include "session/Session.h"

namespace te = tracktion;

namespace sonora
{

static constexpr const char* keyboardDeviceName = "Tastiera Sonora";
static constexpr const char* projectExtension = ".tracktionedit";
static constexpr int statusBarHeight = 26;
static constexpr int detailHeaderHeight = 40;

//==============================================================================
/** Vista arrangement provvisoria: mostra le tracce sulla linea del tempo.
    L'editor completo (clip, registrazione, automazioni) arriva nella fase 4.
*/
class MainComponent::ArrangementPlaceholder final : public juce::Component
{
public:
    explicit ArrangementPlaceholder (MainComponent& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        auto r = getLocalBounds().reduced (14);

        auto ruler = r.removeFromTop (22).withTrimmedLeft (156);
        g.setFont (fonts::mono (10.0f));

        for (int bar = 0; bar < 16; ++bar)
        {
            const int x = ruler.getX() + ruler.getWidth() * bar / 16;
            g.setColour (colours::panelLight);
            g.fillRect (x, ruler.getY(), 1, ruler.getHeight());
            g.setColour (colours::textDim);
            g.drawText (juce::String (bar + 1), x + 4, ruler.getY(), 30, ruler.getHeight(), juce::Justification::centredLeft);
        }

        r.removeFromTop (6);

        if (owner.edit != nullptr)
        {
            for (auto t : te::getAudioTracks (*owner.edit))
            {
                if (r.getHeight() < 56)
                    break;

                auto lane = r.removeFromTop (56);
                r.removeFromTop (6);
                auto header = lane.removeFromLeft (150).toFloat();
                lane.removeFromLeft (6);

                g.setColour (colours::panel);
                g.fillRoundedRectangle (header, metrics::radius);
                auto text = header.reduced (10.0f, 0.0f);
                g.setColour (t->getColour());
                g.fillRoundedRectangle (text.removeFromLeft (8.0f).withSizeKeepingCentre (8.0f, 8.0f), 2.0f);
                text.removeFromLeft (8.0f);
                g.setColour (colours::text);
                g.setFont (fonts::ui (12.5f, fonts::Weight::medium));
                g.drawText (t->getName(), text, juce::Justification::centredLeft, true);

                g.setColour (colours::emptySlot);
                g.fillRoundedRectangle (lane.toFloat(), metrics::radius);
            }
        }

        auto note = getLocalBounds().withSizeKeepingCentre (440, 64);
        g.setColour (colours::panelLight);
        g.fillRoundedRectangle (note.toFloat(), 10.0f);
        g.setColour (colours::text);
        g.setFont (fonts::ui (13.0f, fonts::Weight::medium));
        g.drawText ("L'arrangement arriva nella prossima fase", note.removeFromTop (38), juce::Justification::centredBottom);
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (12.0f));
        g.drawText ("Per ora lavora nella session view (Tab per tornarci)", note, juce::Justification::centredTop);
    }

private:
    MainComponent& owner;
};

//==============================================================================
MainComponent::MainComponent (te::Engine& e)
    : engine (e), topBar (e), browser (e)
{
    arrangement = std::make_unique<ArrangementPlaceholder> (*this);

    topBar.onViewChanged = [this] (TopBar::View v) { setView (v); };
    topBar.onShowMenu = [this] { showMainMenu(); };
    topBar.onShowCommandPalette = [this] { showCommandPalette(); };

    browser.onPluginChosen = [this] (const juce::PluginDescription& d) { insertDevice (d); };

    sessionView.onTrackSelected = [this] (te::AudioTrack* t)
    {
        deviceChain.setTrack (t);
        routeKeyboardTo (t);
        repaint (0, getHeight() - getDetailHeight() - statusBarHeight, getWidth(), detailHeaderHeight);
    };
    sessionView.onStatus = [this] (const juce::String& s) { status (s); };
    sessionView.onClipSelected = [this] (te::Clip* c)
    {
        auto midi = dynamic_cast<te::MidiClip*> (c);
        pianoRoll.setClip (midi);

        if (midi != nullptr)
            setDetail (Detail::clip);
        else if (detail == Detail::clip)
            setDetail (Detail::devices);
    };

    pianoRoll.onPreviewNote = [this] (int pitch, bool on)
    {
        if (on)
            keyboardState.noteOn (1, pitch, 0.8f);
        else
            keyboardState.noteOff (1, pitch, 0.0f);
    };

    for (auto* tab : { &devicesTab, &clipTab })
    {
        tab->setOnColours (colours::hover, colours::text);
        addAndMakeVisible (tab);
    }

    devicesTab.onClick = [this] { setDetail (Detail::devices); };
    clipTab.onClick = [this] { setDetail (Detail::clip); };

    keyboardButton.onClick = [this] { setDetail (detail == Detail::keyboard ? Detail::devices : Detail::keyboard); };

    addDeviceButton.setBaseColour (colours::panel);
    addDeviceButton.onClick = [this] { browser.focusSearch(); };

    keyboard.setAvailableRange (24, 108);
    keyboard.setLowestVisibleKey (48);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, colours::text);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, colours::panel);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, colours::textDim);
    keyboard.setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, colours::accent);
    keyboard.setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, colours::accent.withAlpha (0.3f));
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, colours::panel);
    keyboard.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, colours::textMid);
    keyboardState.addListener (this);

    for (auto* c : std::initializer_list<juce::Component*> { &topBar, &browser, &sessionView, &deviceChain,
                                                             &keyboardButton, &addDeviceButton })
        addAndMakeVisible (c);

    addChildComponent (*arrangement);
    addChildComponent (keyboard);
    addChildComponent (pianoRoll);
    setDetail (Detail::devices);
    addChildComponent (palette);

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

    // Riapre l'ultimo progetto, o quello predefinito.
    auto file = getProjectsFolder().getChildFile (juce::String ("Senza titolo") + projectExtension);
    openProject (file);

    setSize (1440, 900);
    setWantsKeyboardFocus (true);
    startTimerHz (4);
}

MainComponent::~MainComponent()
{
    keyboardState.removeListener (this);
    setEdit (nullptr);
}

juce::File MainComponent::getProjectsFolder()
{
    auto folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Sonora");
    folder.createDirectory();
    return folder;
}

//==============================================================================
void MainComponent::setEdit (std::unique_ptr<te::Edit> newEdit)
{
    sessionView.setEdit (nullptr);
    deviceChain.setTrack (nullptr);
    topBar.setEdit (nullptr);

    if (edit != nullptr)
        edit->getTransport().stop (false, true);

    edit = std::move (newEdit);

    if (edit == nullptr)
        return;

    edit->playInStopEnabled = true;

    if (te::getAudioTracks (*edit).isEmpty())
        edit->ensureNumberOfAudioTracks (4);

    session::ensureGrid (*edit);
    edit->getUndoManager().clearUndoHistory();
    edit->getTransport().ensureContextAllocated();

    topBar.setEdit (edit.get());
    sessionView.setEdit (edit.get());

    if (auto top = getTopLevelComponent(); top != nullptr && top != this)
        top->setName ("Sonora - " + te::EditFileOperations (*edit).getEditFile().getFileNameWithoutExtension());

    repaint();
}

void MainComponent::openProject (const juce::File& file)
{
    std::unique_ptr<te::Edit> e;

    if (file.existsAsFile())
        e = te::loadEditFromFile (engine, file);

    if (e == nullptr)
    {
        e = te::createEmptyEdit (engine, file);
        e->ensureNumberOfAudioTracks (4);

        int i = 0;

        for (auto t : te::getAudioTracks (*e))
        {
            t->setName ("Traccia " + juce::String (++i));
            t->setColour (session::colourForIndex (i - 1));
        }
    }

    setEdit (std::move (e));
    status ("Progetto: " + file.getFileNameWithoutExtension());
}

void MainComponent::newProject()
{
    saveIfChanged();

    auto file = getProjectsFolder().getNonexistentChildFile ("Nuovo progetto", projectExtension, false);
    openProject (file);
    save();
}

void MainComponent::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Apri un progetto", getProjectsFolder(), juce::String ("*") + projectExtension);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
        {
            saveIfChanged();
            openProject (fc.getResult());
        }
    });
}

void MainComponent::save()
{
    if (edit != nullptr)
    {
        te::EditFileOperations (*edit).save (true, true, false);
        status ("Salvato " + te::EditFileOperations (*edit).getEditFile().getFileName());
    }
}

void MainComponent::saveIfChanged()
{
    if (edit != nullptr && edit->hasChangedSinceSaved())
        te::EditFileOperations (*edit).save (false, true, false);
}

void MainComponent::saveAs()
{
    if (edit == nullptr)
        return;

    chooser = std::make_unique<juce::FileChooser> ("Salva il progetto come", te::EditFileOperations (*edit).getEditFile(),
                                                   juce::String ("*") + projectExtension);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
    {
        auto f = fc.getResult();

        if (f == juce::File() || edit == nullptr)
            return;

        f = f.withFileExtension (projectExtension);
        te::EditFileOperations (*edit).saveAs (f, true, nullptr);
        openProject (f);
    });
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // Intestazione del pannello dettaglio
    auto detailRect = getLocalBounds().withTrimmedBottom (statusBarHeight).removeFromBottom (getDetailHeight())
                                  .withTrimmedLeft (metrics::browserWidth);
    g.setColour (colours::sidebar);
    g.fillRect (detailRect);
    g.setColour (colours::divider);
    g.fillRect (detailRect.removeFromTop (1));

    auto header = detailRect.removeFromTop (detailHeaderHeight).reduced (14, 0);

    if (auto t = sessionView.getSelectedTrack())
    {
        g.setColour (t->getColour());
        g.fillRoundedRectangle (header.removeFromLeft (10).withSizeKeepingCentre (10, 10).toFloat(), 3.0f);
        header.removeFromLeft (10);
        g.setColour (colours::text);
        g.setFont (fonts::ui (13.0f, fonts::Weight::semibold));
        const auto name = t->getName();
        const int w = (int) juce::GlyphArrangement::getStringWidth (fonts::ui (13.0f, fonts::Weight::semibold), name) + 12;
        g.drawText (name, header.removeFromLeft (w), juce::Justification::centredLeft);
    }

    // Contenitore delle schede Device / Clip
    auto tabs = devicesTab.getBounds().getUnion (clipTab.getBounds()).expanded (3);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (tabs.toFloat(), 8.0f);

    // Barra di stato
    auto bar = getLocalBounds().removeFromBottom (statusBarHeight);
    g.setColour (colours::background);
    g.fillRect (bar);
    g.setColour (colours::divider);
    g.fillRect (bar.removeFromTop (1));
    bar.reduce (14, 0);

    if (edit != nullptr)
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (11.5f));
        auto project = te::EditFileOperations (*edit).getEditFile().getFileNameWithoutExtension()
                        + (edit->hasChangedSinceSaved() ? "  (modificato)" : "");
        g.drawText (project, bar.removeFromRight (320), juce::Justification::centredRight, true);
    }

    g.setColour (colours::textMid);
    g.setFont (fonts::ui (11.5f));
    g.drawText (statusText, bar, juce::Justification::centredLeft, true);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    palette.setBounds (r);

    topBar.setBounds (r.removeFromTop (metrics::topBarHeight));
    r.removeFromBottom (statusBarHeight);
    browser.setBounds (r.removeFromLeft (metrics::browserWidth));

    auto detailArea = r.removeFromBottom (getDetailHeight());
    auto header = detailArea.removeFromTop (detailHeaderHeight).reduced (14, 6);
    keyboardButton.setBounds (header.removeFromRight (30));
    header.removeFromRight (6);
    addDeviceButton.setBounds (header.removeFromRight (136));

    // Le schede stanno dopo il nome della traccia, disegnato in paint().
    auto tabsArea = header.withTrimmedLeft (180).removeFromLeft (150).reduced (3);
    devicesTab.setBounds (tabsArea.removeFromLeft (72));
    tabsArea.removeFromLeft (2);
    clipTab.setBounds (tabsArea.removeFromLeft (72));

    auto body = detailArea.reduced (14, 0).withTrimmedBottom (12);
    deviceChain.setBounds (body);
    pianoRoll.setBounds (body);
    keyboard.setBounds (body);
    keyboard.setKeyWidth (juce::jmax (12.0f, (float) body.getWidth() / 52.0f));

    sessionView.setBounds (r);
    arrangement->setBounds (r);
}

//==============================================================================
void MainComponent::setView (TopBar::View v)
{
    view = v;
    topBar.setView (v);
    sessionView.setVisible (v == TopBar::View::session);
    arrangement->setVisible (v == TopBar::View::arrangement);
}

int MainComponent::getDetailHeight() const
{
    // Il piano roll ha bisogno di piu' spazio della catena di device.
    return metrics::detailHeight + (detail == Detail::clip ? juce::jmax (0, getHeight() / 5) : 0);
}

void MainComponent::setDetail (Detail d)
{
    detail = d;
    deviceChain.setVisible (d == Detail::devices);
    pianoRoll.setVisible (d == Detail::clip);
    keyboard.setVisible (d == Detail::keyboard);
    addDeviceButton.setVisible (d == Detail::devices);
    devicesTab.setToggleState (d == Detail::devices, juce::dontSendNotification);
    clipTab.setToggleState (d == Detail::clip, juce::dontSendNotification);
    keyboardButton.setToggleState (d == Detail::keyboard, juce::dontSendNotification);
    resized();
    repaint();
}

void MainComponent::togglePlay()
{
    if (edit == nullptr)
        return;

    auto& t = edit->getTransport();

    if (t.isPlaying())
        t.stop (false, false);
    else
        t.play (false);
}

void MainComponent::status (const juce::String& s)
{
    statusText = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (getLocalBounds().removeFromBottom (statusBarHeight));
}

void MainComponent::timerCallback()
{
    // Il messaggio di stato sparisce dopo qualche secondo; il "modificato" si aggiorna.
    if (statusText.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime > 6000)
        statusText.clear();

    repaint (getLocalBounds().removeFromBottom (statusBarHeight));
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown();
    const int code = key.getKeyCode();

    auto is = [&] (char c) { return code == c || code == juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) c); };

    if (key == juce::KeyPress::spaceKey && ! cmd)      { togglePlay(); return true; }
    if (key == juce::KeyPress::tabKey && ! cmd)
    {
        setView (view == TopBar::View::session ? TopBar::View::arrangement : TopBar::View::session);
        return true;
    }

    if (! cmd || edit == nullptr)
        return false;

    if (is ('z') && mods.isShiftDown())   { edit->redo(); return true; }
    if (is ('z'))                          { edit->undo(); return true; }
    if (is ('y'))                          { edit->redo(); return true; }
    if (is ('s') && mods.isShiftDown())   { saveAs(); return true; }
    if (is ('s'))                          { save(); return true; }
    if (is ('o'))                          { openProject(); return true; }
    if (is ('n'))                          { newProject(); return true; }
    if (is ('k'))                          { showCommandPalette(); return true; }
    if (is ('f'))                          { browser.focusSearch(); return true; }

    if (is ('t'))
    {
        if (auto t = session::addTrack (*edit))
            sessionView.selectTrack (t);

        return true;
    }

    return false;
}

//==============================================================================
void MainComponent::showMainMenu()
{
    juce::PopupMenu m;
    m.addItem ("Nuovo progetto", [this] { newProject(); });
    m.addItem ("Apri...", [this] { openProject(); });
    m.addItem ("Salva", [this] { save(); });
    m.addItem ("Salva con nome...", [this] { saveAs(); });
    m.addSeparator();
    m.addItem ("Impostazioni audio e MIDI...", [this] { showAudioSettings(); });
    m.addItem ("Gestisci e scansiona plugin...", [this] { browser.openPluginManager(); });
    m.addSeparator();
    m.addItem ("Esci", [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (juce::Rectangle<int> (12, 44, 1, 1))));
}

void MainComponent::showCommandPalette()
{
    std::vector<CommandPalette::Command> c;

    auto add = [&c] (juce::String name, juce::String shortcut, std::function<void()> fn)
    {
        c.push_back ({ std::move (name), {}, std::move (shortcut), std::move (fn) });
    };

    add ("Riproduci / ferma", "Spazio", [this] { togglePlay(); });
    add ("Ferma tutte le clip", {}, [this] { if (edit != nullptr) session::stopAllClips (*edit); });
    add ("Nuova traccia", "Ctrl+T", [this]
    {
        if (edit != nullptr)
            if (auto t = session::addTrack (*edit))
                sessionView.selectTrack (t);
    });
    add ("Nuova scena", {}, [this] { if (edit != nullptr) session::addScene (*edit); });
    add ("Annulla", "Ctrl+Z", [this] { if (edit != nullptr) edit->undo(); });
    add ("Ripeti", "Ctrl+Shift+Z", [this] { if (edit != nullptr) edit->redo(); });
    add ("Session view", "Tab", [this] { setView (TopBar::View::session); });
    add ("Arrangement", "Tab", [this] { setView (TopBar::View::arrangement); });
    add ("Metronomo", {}, [this] { if (edit != nullptr) edit->clickTrackEnabled = ! edit->clickTrackEnabled.get(); });
    add ("Mostra o nascondi la tastiera", {}, [this] { keyboardButton.triggerClick(); });
    add ("Mostra la catena di device", {}, [this] { setDetail (Detail::devices); });
    add ("Mostra il piano roll della clip", {}, [this] { setDetail (Detail::clip); });
    add ("Nuovo progetto", "Ctrl+N", [this] { newProject(); });
    add ("Apri progetto", "Ctrl+O", [this] { openProject(); });
    add ("Salva", "Ctrl+S", [this] { save(); });
    add ("Salva con nome", "Ctrl+Shift+S", [this] { saveAs(); });
    add ("Impostazioni audio e MIDI", {}, [this] { showAudioSettings(); });
    add ("Gestisci e scansiona plugin", {}, [this] { browser.openPluginManager(); });
    add ("Cerca nel browser", "Ctrl+F", [this] { browser.focusSearch(); });

    for (auto& d : browser.getAllDevices())
        c.push_back ({ "Aggiungi " + d.name, d.pluginFormatName, {}, [this, d] { insertDevice (d); } });

    palette.show (std::move (c));
}

//==============================================================================
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

void MainComponent::insertDevice (const juce::PluginDescription& desc)
{
    auto track = sessionView.getSelectedTrack();

    if (track == nullptr || edit == nullptr)
    {
        status ("Seleziona prima una traccia");
        return;
    }

    te::Plugin::Ptr plugin;

    if (desc.pluginFormatName == PluginBrowserComponent::builtInFormat)
        plugin = edit->getPluginCache().createNewPlugin (desc.fileOrIdentifier, {});
    else
        plugin = edit->getPluginCache().createNewPlugin (te::ExternalPlugin::xmlTypeName, desc);

    if (plugin == nullptr)
    {
        status ("Impossibile creare " + desc.name);
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

    edit->getUndoManager().beginNewTransaction ("Aggiungi " + desc.name);
    track->pluginList.insertPlugin (plugin, index, nullptr);

    if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get());
        external != nullptr && external->getAudioPluginInstance() == nullptr)
        status ("Errore nel caricare " + desc.name + ": " + external->getLoadError());
    else
        status ("Aggiunto " + desc.name + " a " + track->getName());
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
