/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MainComponent.h"
#include "Animation.h"
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
MainComponent::MainComponent (te::Engine& e)
    : engine (e), topBar (e), browser (e)
{
    arrangement.getSelectedTrack = [this] { return sessionView.getSelectedTrack(); };
    arrangement.onTrackSelected = [this] (te::AudioTrack* t) { sessionView.selectTrack (t); };
    arrangement.onStatus = [this] (const juce::String& s) { status (s); };

    topBar.onViewChanged = [this] (TopBar::View v) { setView (v); };
    topBar.onShowMenu = [this] { showMainMenu(); };
    topBar.onShowCommandPalette = [this] { showCommandPalette(); };
    topBar.onTogglePlay = [this] { togglePlay(); };
    topBar.onStop = [this] { stopTransport (true); };
    topBar.onRecord = [this] { toggleRecord(); };
    topBar.onCapture = [this] { captureNow(); };
    topBar.canCapture = [this]
    {
        if (capturer == nullptr || recorder == nullptr || edit == nullptr)
            return false;

        for (auto t : te::getAudioTracks (*edit))
            if (recorder->isArmed (*t) && capturer->hasSomethingFor (*t))
                return true;

        return false;
    };
    topBar.isRecording = [this] { return recorder != nullptr && recorder->isRecording(); };

    browser.onPluginChosen = [this] (const juce::PluginDescription& d) { insertDevice (d); };

    sessionView.onTrackSelected = [this] (te::AudioTrack* t)
    {
        deviceChain.setTrack (t);
        if (recorder != nullptr)
            recorder->setSelectedTrack (t);
        repaint (0, getHeight() - getDetailHeight() - statusBarHeight, getWidth(), detailHeaderHeight);
    };
    sessionView.onStatus = [this] (const juce::String& s) { status (s); };
    sessionView.onClipSelected = [this] (te::Clip* c) { showClip (c); };
    arrangement.onClipSelected = [this] (te::Clip* c) { showClip (c); };

    showClip = [this] (te::Clip* c)
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

    addChildComponent (arrangement);
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
    sessionView.setRecorder (nullptr);
    arrangement.setRecorder (nullptr);
    sessionView.setEdit (nullptr);
    arrangement.setEdit (nullptr);
    pianoRoll.setClip (nullptr);
    deviceChain.setTrack (nullptr);
    topBar.setEdit (nullptr);

    if (recorder != nullptr)
        recorder->stopAllSlotRecordings();

    if (edit != nullptr)
        edit->getTransport().stop (false, true);

    recorder.reset();

    if (capturer != nullptr)
        capturer->setEdit (nullptr);

    edit = std::move (newEdit);

    if (edit == nullptr)
        return;

    edit->playInStopEnabled = true;

    if (te::getAudioTracks (*edit).isEmpty())
        edit->ensureNumberOfAudioTracks (4);

    session::ensureGrid (*edit);
    edit->getUndoManager().clearUndoHistory();
    edit->getTransport().ensureContextAllocated();

    recorder = std::make_unique<recording::Recorder> (*edit);

    if (capturer == nullptr)
        capturer = std::make_unique<capture::Capturer> (engine);

    capturer->setEdit (edit.get());
    recorder->onSlotRecorded = [this] (te::Clip& c)
    {
        if (auto slot = c.getClipSlot())
            status ("Registrata la clip \"" + c.getName() + "\" su " + slot->track.getName());
    };

    topBar.setEdit (edit.get());
    sessionView.setRecorder (recorder.get());
    arrangement.setRecorder (recorder.get());
    sessionView.setEdit (edit.get());
    arrangement.setEdit (edit.get());
    recorder->setSelectedTrack (sessionView.getSelectedTrack());

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

    session::setUpFilePaths (*e);
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
    arrangement.setBounds (r);
}

//==============================================================================
void MainComponent::setView (TopBar::View v)
{
    view = v;
    topBar.setView (v);
    juce::Component& shown = v == TopBar::View::session ? (juce::Component&) sessionView : arrangement;
    juce::Component& hidden = v == TopBar::View::session ? (juce::Component&) arrangement : sessionView;
    juce::Desktop::getInstance().getAnimator().cancelAnimation (&hidden, false);
    hidden.setVisible (false);
    hidden.setAlpha (1.0f);

    if (! shown.isVisible())
        anim::fadeIn (shown, 140);

    if (v == TopBar::View::arrangement)
        arrangement.grabKeyboardFocus();
}

int MainComponent::targetDetailHeight() const
{
    // Il piano roll ha bisogno di piu' spazio della catena di device.
    return metrics::detailHeight + (detail == Detail::clip ? juce::jmax (0, getHeight() / 5) : 0);
}

int MainComponent::getDetailHeight() const
{
    return detailAnimating ? juce::roundToInt (detailHeightNow) : targetDetailHeight();
}

void MainComponent::animateDetail()
{
    if (! detailAnimating)
        return;

    if (! anim::approach (detailHeightNow, (float) targetDetailHeight(), 0.3f))
        detailAnimating = false;

    resized();
    repaint();
}

void MainComponent::setDetail (Detail d)
{
    if (isShowing() && d != detail)
    {
        detailHeightNow = (float) getDetailHeight();
        detailAnimating = true;
    }

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

    if (edit->getTransport().isPlaying())
        stopTransport (false);
    else
        edit->getTransport().play (false);
}

void MainComponent::stopTransport (bool returnToStartIfStopped)
{
    if (edit == nullptr)
        return;

    auto& t = edit->getTransport();

    if (t.isPlaying())
    {
        // Le registrazioni negli slot si chiudono prima che il trasporto si fermi.
        if (recorder != nullptr)
            recorder->stopAllSlotRecordings();

        t.stop (false, false);
    }
    else if (returnToStartIfStopped)
    {
        t.setPosition (te::TimePosition());
    }
}

void MainComponent::toggleRecord()
{
    if (recorder == nullptr)
        return;

    const bool wasRecording = edit->getTransport().isRecording();

    if (auto res = recorder->toggleArrangementRecording(); res.failed())
        status (res.getErrorMessage());
    else if (! wasRecording)
        status ("Registrazione nell'arrangement");
}

void MainComponent::captureNow()
{
    if (capturer == nullptr || recorder == nullptr || edit == nullptr)
        return;

    const auto dest = view == TopBar::View::session ? capture::Destination::session : capture::Destination::arrangement;
    juce::Array<te::Clip*> clips;

    if (auto res = capturer->captureArmedTracks (*recorder, dest, &clips); res.failed())
    {
        status (res.getErrorMessage());
        return;
    }

    auto clip = clips.getFirst();

    if (clip == nullptr)
        return;

    auto track = clip->getClipSlot() != nullptr ? dynamic_cast<te::AudioTrack*> (&clip->getClipSlot()->track)
                                                : dynamic_cast<te::AudioTrack*> (clip->getTrack());

    if (auto slot = clip->getClipSlot(); slot != nullptr && track != nullptr)
        sessionView.selectSlot (track, track->getClipSlotList().getClipSlots().indexOf (slot));

    showClip (clip);
    status (clips.size() == 1 ? "Catturata una clip su " + (track != nullptr ? track->getName() : juce::String())
                              : "Catturate " + juce::String (clips.size()) + " clip");
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
    if (key == juce::KeyPress::F9Key)                   { toggleRecord(); return true; }
    if (cmd && mods.isShiftDown() && is ('c'))          { captureNow(); return true; }
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
    add ("Registra nell'arrangement", "F9", [this] { toggleRecord(); });
    add ("Cattura quello che hai appena suonato", "Ctrl+Shift+C", [this] { captureNow(); });
    add ("Arma la traccia selezionata", {}, [this]
    {
        if (auto t = sessionView.getSelectedTrack(); t != nullptr && recorder != nullptr)
            recorder->setArmed (*t, ! recording::isExplicitlyArmed (*t));
    });
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

    if (capturer != nullptr)
        capturer->addLiveMidi (juce::MidiMessage::noteOn (channel, note, velocity));
}

void MainComponent::handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity)
{
    if (keyboardDevice != nullptr)
        keyboardDevice->handleIncomingMidiMessage (nullptr, juce::MidiMessage::noteOff (channel, note, velocity));

    if (capturer != nullptr)
        capturer->addLiveMidi (juce::MidiMessage::noteOff (channel, note, velocity));
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
