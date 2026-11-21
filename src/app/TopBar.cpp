/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "TopBar.h"
#include "session/Session.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    juce::Rectangle<int> logoArea (juce::Rectangle<int> r)    { return r.removeFromLeft (150); }
}

TopBar::TopBar (te::Engine& e) : engine (e)
{
    menuButton.onClick = [this] { if (onShowMenu) onShowMenu(); };

    for (auto* b : { &sessionButton, &arrangementButton })
    {
        b->setOnColours (colours::hover, colours::text);
        addAndMakeVisible (b);
    }

    sessionButton.onClick = [this] { setView (View::session); if (onViewChanged) onViewChanged (View::session); };
    arrangementButton.onClick = [this] { setView (View::arrangement); if (onViewChanged) onViewChanged (View::arrangement); };
    setView (View::session);

    undoButton.onClick = [this] { if (edit != nullptr) edit->undo(); };
    redoButton.onClick = [this] { if (edit != nullptr) edit->redo(); };

    playButton.setActiveColour (colours::play);
    playButton.onClick = [this]
    {
        if (edit == nullptr)
            return;

        auto& t = edit->getTransport();

        if (t.isPlaying())
            t.stop (false, false);
        else
            t.play (false);
    };

    stopButton.onClick = [this]
    {
        if (edit == nullptr)
            return;

        auto& t = edit->getTransport();

        if (t.isPlaying())
            t.stop (false, false);
        else
            t.setPosition (te::TimePosition());
    };

    recordButton.setIconColour (colours::record);
    recordButton.setEnabled (false);

    loopButton.setClickingTogglesState (false);
    loopButton.onClick = [this]
    {
        if (edit != nullptr)
            edit->getTransport().looping = ! edit->getTransport().looping.get();
    };

    metronomeButton.onClick = [this]
    {
        if (edit != nullptr)
            edit->clickTrackEnabled = ! edit->clickTrackEnabled.get();
    };

    tempo.onChange = [this] (double bpm)
    {
        if (edit != nullptr)
        {
            edit->getUndoManager().beginNewTransaction ("Tempo");
            edit->tempoSequence.getTempos()[0]->setBpm (bpm);
        }
    };

    timeSigButton.setBaseColour (colours::panel);
    timeSigButton.setTooltip ("Metro");
    timeSigButton.onClick = [this] { showTimeSigMenu(); };

    quantisationButton.setBaseColour (colours::panel);
    quantisationButton.setTooltip ("Quantizzazione del lancio delle clip");
    quantisationButton.onClick = [this] { showQuantisationMenu(); };

    paletteButton.setBaseColour (colours::panel);
    paletteButton.setTooltip ("Palette comandi (Ctrl+K)");
    paletteButton.onClick = [this] { if (onShowCommandPalette) onShowCommandPalette(); };

    for (auto* c : std::initializer_list<juce::Component*> { &menuButton, &undoButton, &redoButton, &playButton, &stopButton,
                                                             &recordButton, &loopButton, &metronomeButton, &tempo,
                                                             &timeSigButton, &quantisationButton, &paletteButton })
        addAndMakeVisible (c);

    startTimerHz (20);
}

TopBar::~TopBar() = default;

void TopBar::setEdit (te::Edit* e)
{
    edit = e;
    timerCallback();
}

void TopBar::setView (View v)
{
    sessionButton.setToggleState (v == View::session, juce::dontSendNotification);
    arrangementButton.setToggleState (v == View::arrangement, juce::dontSendNotification);
}

void TopBar::timerCallback()
{
    auto cpu = "CPU " + juce::String (juce::roundToInt (engine.getDeviceManager().getCpuUsage() * 100.0)) + "%";
    juce::String position = "1. 1. 1";

    if (edit != nullptr)
    {
        auto& t = edit->getTransport();
        auto& ts = edit->tempoSequence;
        const auto bb = ts.toBarsAndBeats (t.getPosition());
        position = juce::String (bb.bars + 1) + ". " + juce::String (bb.getWholeBeats() + 1) + ". "
                    + juce::String ((int) (bb.getFractionalBeats().inBeats() * 4.0) + 1);

        playButton.setToggleState (t.isPlaying(), juce::dontSendNotification);
        loopButton.setToggleState (t.looping.get(), juce::dontSendNotification);
        metronomeButton.setToggleState (edit->clickTrackEnabled.get(), juce::dontSendNotification);
        undoButton.setEnabled (edit->getUndoManager().canUndo());
        redoButton.setEnabled (edit->getUndoManager().canRedo());

        if (! tempo.isMouseButtonDown())
            tempo.setValue (ts.getTempos()[0]->getBpm(), juce::dontSendNotification);

        auto& sig = *ts.getTimeSigs()[0];
        timeSigButton.setButtonText (juce::String (sig.numerator.get()) + "/" + juce::String (sig.denominator.get()));
        quantisationButton.setButtonText (session::getQuantisationNames()[(int) edit->getLaunchQuantisation().type.get()]);
    }

    if (position != positionText || cpu != cpuText)
    {
        positionText = position;
        cpuText = cpu;
        repaint();
    }
}

void TopBar::showTimeSigMenu()
{
    if (edit == nullptr)
        return;

    juce::PopupMenu m;

    for (auto s : { "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "12/8" })
        m.addItem (s, [this, s]
        {
            if (edit != nullptr)
            {
                edit->getUndoManager().beginNewTransaction ("Metro");
                edit->tempoSequence.getTimeSigs()[0]->setStringTimeSig (s);
            }
        });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&timeSigButton));
}

void TopBar::showQuantisationMenu()
{
    if (edit == nullptr)
        return;

    juce::PopupMenu m;
    auto names = session::getQuantisationNames();
    const auto current = (int) edit->getLaunchQuantisation().type.get();

    // Le voci piu' usate; le terzine e le puntate restano disponibili nel sottomenu.
    for (auto t : { te::LaunchQType::none, te::LaunchQType::eightBars, te::LaunchQType::fourBars, te::LaunchQType::twoBars,
                    te::LaunchQType::bar, te::LaunchQType::half, te::LaunchQType::quarter, te::LaunchQType::eighth,
                    te::LaunchQType::sixteenth })
        m.addItem (names[(int) t], true, current == (int) t, [this, t] { if (edit != nullptr) edit->getLaunchQuantisation().type = t; });

    juce::PopupMenu more;

    for (int i = 0; i < names.size(); ++i)
        more.addItem (names[i], true, current == i, [this, i]
        {
            if (edit != nullptr)
                edit->getLaunchQuantisation().type = (te::LaunchQType) i;
        });

    m.addSeparator();
    m.addSubMenu ("Altri valori", more);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&quantisationButton));
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::divider);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // Logo: un'onda in un cerchio.
    auto logo = logoArea (getLocalBounds().reduced (12, 0)).withTrimmedLeft (40);
    auto mark = logo.removeFromLeft (22).withSizeKeepingCentre (20, 20).toFloat();
    g.setColour (colours::accent);
    g.drawEllipse (mark.reduced (1.0f), 1.8f);
    juce::Path wave;
    wave.startNewSubPath (mark.getX() + 5.0f, mark.getCentreY() + 2.0f);
    wave.quadraticTo (mark.getX() + 7.5f, mark.getY() + 4.0f, mark.getCentreX(), mark.getCentreY());
    wave.quadraticTo (mark.getRight() - 7.5f, mark.getBottom() - 4.0f, mark.getRight() - 5.0f, mark.getCentreY() - 2.0f);
    g.setColour (colours::text);
    g.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    logo.removeFromLeft (8);
    g.setFont (fonts::ui (14.0f, fonts::Weight::semibold));
    g.drawText ("Sonora", logo, juce::Justification::centredLeft);

    // Contenitore delle viste
    auto views = sessionButton.getBounds().getUnion (arrangementButton.getBounds()).expanded (3);
    g.setColour (colours::panel);
    g.fillRoundedRectangle (views.toFloat(), 8.0f);

    // Posizione
    auto pos = juce::Rectangle<int> (tempo.getX() - 104, tempo.getY(), 98, tempo.getHeight());
    g.fillRoundedRectangle (pos.toFloat(), metrics::radius);
    g.setColour (colours::text);
    g.setFont (fonts::mono (13.0f));
    g.drawText (positionText, pos, juce::Justification::centred);

    g.setColour (colours::textDim);
    g.setFont (fonts::mono (11.0f));
    g.drawText (cpuText, getLocalBounds().reduced (16, 0).removeFromRight (64), juce::Justification::centredRight);
}

void TopBar::paintOverChildren (juce::Graphics& g)
{
    // Icona della palette e scorciatoia
    auto pr = paletteButton.getBounds();
    icons::draw (g, icons::search(), pr.removeFromLeft (30).withSizeKeepingCentre (13, 13).toFloat(), colours::textDim);
    auto shortcut = pr.removeFromRight (52).withSizeKeepingCentre (44, 18);
    g.setColour (colours::panelLight);
    g.fillRoundedRectangle (shortcut.toFloat(), 4.0f);
    g.setColour (colours::textMid);
    g.setFont (fonts::mono (10.5f));
    g.drawText ("Ctrl K", shortcut, juce::Justification::centred);

}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (12, 10);
    menuButton.setBounds (r.removeFromLeft (32));
    r.removeFromLeft (logoArea (r).getWidth() - 32);

    auto viewsArea = r.removeFromLeft (200).reduced (3);
    sessionButton.setBounds (viewsArea.removeFromLeft (84));
    viewsArea.removeFromLeft (2);
    arrangementButton.setBounds (viewsArea.removeFromLeft (110));

    r.removeFromLeft (12);
    undoButton.setBounds (r.removeFromLeft (32));
    redoButton.setBounds (r.removeFromLeft (32));

    r.removeFromRight (72);   // CPU
    paletteButton.setBounds (r.removeFromRight (juce::jlimit (160, 300, getWidth() / 5)));
    paletteButton.setButtonText (paletteButton.getWidth() > 240 ? "Cerca azioni, device, plugin" : "Cerca");

    // Gruppo centrale: trasporto, posizione, tempo, metro, quantizzazione
    const int groupWidth = 5 * 34 + 12 + 104 + 112 + 6 + 52 + 6 + 96;
    auto group = r.withSizeKeepingCentre (juce::jmin (groupWidth, r.getWidth()), r.getHeight());

    for (auto* b : { &playButton, &stopButton, &recordButton, &loopButton, &metronomeButton })
        b->setBounds (group.removeFromLeft (34));

    group.removeFromLeft (12 + 104);
    tempo.setBounds (group.removeFromLeft (112));
    group.removeFromLeft (6);
    timeSigButton.setBounds (group.removeFromLeft (52));
    group.removeFromLeft (6);
    quantisationButton.setBounds (group.removeFromLeft (96));
}

} // namespace sonora
