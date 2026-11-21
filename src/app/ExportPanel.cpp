/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "ExportPanel.h"
#include "Animation.h"
#include "session/Arrangement.h"

namespace te = tracktion;

namespace sonora
{

namespace ex = exporting;

//==============================================================================
void Segmented::setOptions (juce::StringArray o, int s)
{
    options = std::move (o);
    enabled.clearQuick();

    for (int i = 0; i < options.size(); ++i)
        enabled.add (true);

    selected = juce::jlimit (0, juce::jmax (0, options.size() - 1), s);
    repaint();
}

void Segmented::setSelected (int s)
{
    selected = juce::jlimit (0, juce::jmax (0, options.size() - 1), s);
    repaint();
}

void Segmented::setEnabled (int index, bool e)
{
    if (juce::isPositiveAndBelow (index, enabled.size()))
        enabled.set (index, e);

    repaint();
}

juce::Rectangle<float> Segmented::optionArea (int i) const
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const float w = r.getWidth() / (float) juce::jmax (1, options.size());
    return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
}

void Segmented::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    const auto mouse = getMouseXYRelative().toFloat();

    for (int i = 0; i < options.size(); ++i)
    {
        auto r = optionArea (i);
        const bool on = i == selected;
        const bool hover = isMouseOver() && r.contains (mouse) && enabled[i];

        if (on || hover)
        {
            g.setColour (on ? colours::hover : colours::panelLight);
            g.fillRoundedRectangle (r, 6.0f);
        }

        g.setColour (! enabled[i] ? colours::textDim.withAlpha (0.45f) : (on ? colours::text : colours::textMid));
        g.setFont (fonts::ui (12.0f, on ? fonts::Weight::medium : fonts::Weight::regular));
        g.drawText (options[i], r.reduced (6.0f, 0.0f), juce::Justification::centred, true);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < options.size(); ++i)
    {
        if (optionArea (i).contains (e.position) && enabled[i] && i != selected)
        {
            selected = i;
            repaint();

            if (onChange)
                onChange (i);
        }
    }
}

//==============================================================================
void Switch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto track = r.removeFromLeft (30.0f).withSizeKeepingCentre (30.0f, 18.0f);
    g.setColour (on ? colours::accent : colours::hover);
    g.fillRoundedRectangle (track, 9.0f);

    const float knob = 14.0f;
    const float x = on ? track.getRight() - knob - 2.0f : track.getX() + 2.0f;
    g.setColour (on ? colours::ink : colours::textMid);
    g.fillEllipse (x, track.getCentreY() - knob / 2.0f, knob, knob);

    r.removeFromLeft (10.0f);
    g.setColour (colours::textMid);
    g.setFont (fonts::ui (12.5f));
    g.drawText (label, r, juce::Justification::centredLeft, true);
}

void Switch::mouseDown (const juce::MouseEvent&)
{
    on = ! on;
    repaint();

    if (onChange)
        onChange();
}

//==============================================================================
namespace
{
    constexpr int panelWidth = 600;
    constexpr int headerHeight = 64;
    constexpr int rowHeight = 40;
    constexpr int footerHeight = 64;
    constexpr int labelWidth = 120;
    constexpr int numRows = 8;

    const ex::Format formats[] = { ex::Format::wav, ex::Format::flac, ex::Format::mp3, ex::Format::ogg };
    const int mp3Rates[] = { 192, 256, 320 };

    juce::Array<double> ratesFor (ex::Format f)
    {
        if (f == ex::Format::mp3 || f == ex::Format::ogg)
            return { 44100.0, 48000.0 };

        return { 44100.0, 48000.0, 96000.0 };
    }

    juce::String formatTime (double seconds)
    {
        const int s = juce::roundToInt (seconds);
        return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
}

ExportPanel::ExportPanel()
{
    what.setOptions ({ "Mix", "Stem: una traccia per file" }, 0);
    what.onChange = [this] (int i) { settings.stems = i == 1; destinationChosen = false; refresh(); };

    range.setOptions ({ "Tutto il brano", "Solo il loop" }, 0);
    range.onChange = [this] (int i) { settings.range = i == 0 ? ex::Range::song : ex::Range::loop; refresh(); };

    juce::StringArray formatNames;

    for (auto f : formats)
        formatNames.add (ex::nameFor (f));

    format.setOptions (formatNames, 0);
    format.onChange = [this] (int i)
    {
        settings.format = formats[i];

        if (settings.destination != juce::File() && ! settings.stems)
            settings.destination = settings.destination.withFileExtension (ex::extensionFor (settings.format));

        refresh();
    };

    quality.onChange = [this] (int i)
    {
        if (settings.format == ex::Format::mp3)
            settings.mp3Kbps = mp3Rates[i];
        else if (auto depths = ex::bitDepthsFor (settings.format); ! depths.isEmpty())
            settings.bitDepth = depths[i];

        refresh();
    };

    rate.onChange = [this] (int i) { settings.sampleRate = ratesFor (settings.format)[i]; refresh(); };

    normalise.onChange = [this] { settings.normalise = normalise.isOn(); };
    tails.setOn (settings.includeTails);
    tails.onChange = [this] { settings.includeTails = tails.isOn(); };

    chooseButton.setBaseColour (colours::panel);
    chooseButton.onClick = [this] { chooseDestination(); };

    cancelButton.setBaseColour (colours::panel);
    cancelButton.onClick = [this]
    {
        if (stage == Stage::running)
            exporter.cancel();
        else
            dismiss();
    };

    exportButton.setBaseColour (colours::accent);
    exportButton.setOnColours (colours::accent.brighter (0.2f), colours::ink);
    exportButton.setToggleState (true, juce::dontSendNotification);
    exportButton.onClick = [this] { startExport(); };

    revealButton.setBaseColour (colours::panel);
    revealButton.onClick = [this]
    {
        if (! resultFiles.isEmpty())
            resultFiles.getFirst().revealToUser();
    };

    for (auto* c : std::initializer_list<juce::Component*> { &what, &range, &format, &quality, &rate, &normalise, &tails,
                                                             &chooseButton, &cancelButton, &exportButton, &revealButton })
        addAndMakeVisible (c);

    exporter.onFinished = [this] (juce::Result r, juce::Array<juce::File> files) { finished (r, files); };
    setWantsKeyboardFocus (true);
}

ExportPanel::~ExportPanel()
{
    stopTimer();
}

void ExportPanel::show (te::Edit& e)
{
    if (stage == Stage::running && edit == &e)
    {
        anim::fadeIn (*this, 120);
        toFront (true);
        return;
    }

    if (edit != &e)
        destinationChosen = false;

    edit = &e;
    stage = Stage::settings;
    resultText = {};

    refresh();
    anim::fadeIn (*this, 120);
    toFront (true);
    grabKeyboardFocus();
}

void ExportPanel::dismiss()
{
    if (stage == Stage::done)
        stage = Stage::settings;

    setVisible (false);
}

void ExportPanel::refresh()
{
    if (edit == nullptr)
        return;

    const bool running = stage == Stage::running, done = stage == Stage::done;

    // Qualita' secondo il formato
    if (settings.format == ex::Format::mp3)
    {
        int sel = 0;

        for (int i = 0; i < 3; ++i)
            if (mp3Rates[i] == settings.mp3Kbps)
                sel = i;

        quality.setOptions ({ "192 kb/s", "256 kb/s", "320 kb/s" }, sel);
    }
    else if (auto depths = ex::bitDepthsFor (settings.format); ! depths.isEmpty())
    {
        juce::StringArray names;

        for (auto d : depths)
            names.add (d == 32 ? "32 bit float" : juce::String (d) + " bit");

        if (! depths.contains (settings.bitDepth))
            settings.bitDepth = depths.getLast();

        quality.setOptions (names, depths.indexOf (settings.bitDepth));
    }
    else
    {
        quality.setOptions ({ juce::String::fromUTF8 ("Alta qualit\xc3\xa0") }, 0);
    }

    auto rates = ratesFor (settings.format);

    if (! rates.contains (settings.sampleRate))
        settings.sampleRate = 44100.0;

    juce::StringArray rateNames;

    for (auto r : rates)
        rateNames.add (juce::String (r / 1000.0, juce::approximatelyEqual (r, 44100.0) ? 1 : 0) + " kHz");

    rate.setOptions (rateNames, rates.indexOf (settings.sampleRate));

    // MP3 solo con la libreria LAME
    const bool mp3 = ex::isAvailable (edit->engine, ex::Format::mp3);
    format.setEnabled (2, mp3);
    format.setTooltip (mp3 ? juce::String() : "Per l'MP3 serve la libreria LAME: su Arch \"pacman -S lame\", "
                                               "su Windows libmp3lame.dll accanto al programma");

    const bool hasLoop = ex::timeRangeFor (*edit, ex::Range::loop).has_value();
    range.setEnabled (1, hasLoop);

    if (! hasLoop && settings.range == ex::Range::loop)
    {
        settings.range = ex::Range::song;
        range.setSelected (0);
    }

    if (! destinationChosen)
        settings.destination = ex::defaultDestination (*edit, settings);

    for (auto* c : std::initializer_list<juce::Component*> { &what, &range, &format, &quality, &rate, &normalise, &tails, &chooseButton })
        c->setVisible (! running && ! done);

    exportButton.setVisible (! running && ! done);
    exportButton.setEnabled (ex::timeRangeFor (*edit, settings.range).has_value());
    revealButton.setVisible (done && ! resultFiles.isEmpty());
    cancelButton.setButtonText (running ? "Ferma" : (done ? "Chiudi" : "Annulla"));

    resized();
    repaint();
}

juce::String ExportPanel::rangeDescription() const
{
    if (edit == nullptr)
        return {};

    auto r = ex::timeRangeFor (*edit, settings.range);

    if (! r)
        return juce::String::fromUTF8 (settings.range == ex::Range::loop ? "Il loop non \xc3\xa8 impostato"
                                                                         : "L'arrangement \xc3\xa8 vuoto: non c'\xc3\xa8 niente da esportare");

    auto& ts = edit->tempoSequence;
    const auto a = ts.toBarsAndBeats (r->getStart()), b = ts.toBarsAndBeats (r->getEnd());
    const int lastBar = b.getFractionalBeats().inBeats() > 1.0e-3 || b.getWholeBeats() > 0 ? b.bars + 1 : b.bars;
    return "Battute " + juce::String (a.bars + 1) + "-" + juce::String (juce::jmax (a.bars + 1, lastBar))
           + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + formatTime (r->getLength().inSeconds());
}

void ExportPanel::chooseDestination()
{
    if (edit == nullptr)
        return;

    juce::Component::SafePointer<ExportPanel> safe (this);

    if (settings.stems)
    {
        chooser = std::make_unique<juce::FileChooser> ("Cartella per gli stem", settings.destination.getParentDirectory());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult() != juce::File())
            {
                safe->settings.destination = fc.getResult();
                safe->destinationChosen = true;
                safe->refresh();
            }
        });
    }
    else
    {
        chooser = std::make_unique<juce::FileChooser> ("Esporta il mix", settings.destination,
                                                       "*" + ex::extensionFor (settings.format));
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe] (const juce::FileChooser& fc)
        {
            if (safe != nullptr && fc.getResult() != juce::File())
            {
                safe->settings.destination = fc.getResult().withFileExtension (ex::extensionFor (safe->settings.format));
                safe->destinationChosen = true;
                safe->refresh();
            }
        });
    }
}

void ExportPanel::startExport()
{
    if (edit == nullptr)
        return;

    // Il render vuole il motore fermo
    edit->getTransport().stop (false, true);

    if (auto r = exporter.start (*edit, settings); r.failed())
    {
        resultText = r.getErrorMessage();
        repaint();
        return;
    }

    resultText = {};
    resultFiles.clear();
    stage = Stage::running;
    startTimerHz (20);
    refresh();
}

void ExportPanel::finished (juce::Result r, juce::Array<juce::File> files)
{
    stopTimer();
    resultFiles = files;
    stage = Stage::done;

    if (r.wasOk())
    {
        resultText = files.size() == 1 ? "Esportato " + files.getFirst().getFileName()
                                       : "Esportati " + juce::String (files.size()) + " stem in "
                                           + files.getFirst().getParentDirectory().getFileName();
    }
    else
    {
        resultText = r.getErrorMessage();
    }

    if (onStatus)
        onStatus (resultText);

    refresh();

    if (! isVisible() && r.wasOk())
        stage = Stage::settings;
}

void ExportPanel::timerCallback()
{
    repaint();
}

//==============================================================================
juce::Rectangle<int> ExportPanel::panelArea() const
{
    auto r = getLocalBounds();
    const int w = juce::jmin (panelWidth, r.getWidth() - 32);
    const int h = stage == Stage::settings ? headerHeight + numRows * rowHeight + footerHeight + 8
                                           : headerHeight + 96 + footerHeight;
    return { r.getCentreX() - w / 2, juce::jmax (16, (r.getHeight() - h) / 3), w, h };
}

void ExportPanel::resized()
{
    auto panel = panelArea().reduced (24, 0);
    auto footer = panel.removeFromBottom (footerHeight).withSizeKeepingCentre (panel.getWidth(), 32);
    exportButton.setBounds (footer.removeFromRight (110));
    footer.removeFromRight (8);
    cancelButton.setBounds (footer.removeFromRight (90));
    footer.removeFromRight (8);
    revealButton.setBounds (footer.removeFromRight (170));

    panel.removeFromTop (headerHeight);
    auto row = [&panel]
    {
        auto r = panel.removeFromTop (rowHeight).withTrimmedLeft (labelWidth);
        return r.withSizeKeepingCentre (r.getWidth(), 32);
    };

    what.setBounds (row());
    range.setBounds (row());
    format.setBounds (row());
    quality.setBounds (row());
    rate.setBounds (row());
    normalise.setBounds (row());
    tails.setBounds (row());
    auto dest = row();
    chooseButton.setBounds (dest.removeFromRight (90));
}

void ExportPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));

    const auto area = panelArea();
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 28, { 0, 10 }).drawForRectangle (g, area);
    g.setColour (colours::panelLight);
    g.fillRoundedRectangle (area.toFloat(), 14.0f);

    auto panel = area.reduced (24, 0);
    auto header = panel.removeFromTop (headerHeight).reduced (0, 14);
    g.setColour (colours::text);
    g.setFont (fonts::ui (16.0f, fonts::Weight::semibold));
    g.drawText ("Esporta audio", header.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (fonts::ui (12.0f));
    g.drawText (rangeDescription(), header, juce::Justification::bottomLeft, true);

    auto footer = panel.removeFromBottom (footerHeight);

    if (stage == Stage::settings)
    {
        const juce::String labels[] = { "Cosa", "Parte", "Formato", juce::String::fromUTF8 ("Qualit\xc3\xa0"), "Frequenza", "Opzioni", "", "Salva in" };

        for (int i = 0; i < numRows; ++i)
        {
            auto r = panel.removeFromTop (rowHeight);
            g.setColour (colours::textDim);
            g.setFont (fonts::ui (12.5f));
            g.drawText (labels[i], r.removeFromLeft (labelWidth), juce::Justification::centredLeft);

            if (i == numRows - 1 && edit != nullptr)
            {
                auto path = settings.destination.getFullPathName();
                const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();

                if (path.startsWith (home))
                    path = "~" + path.substring (home.length());

                if (settings.stems)
                    path += juce::File::getSeparatorString();

                g.setColour (colours::textMid);
                g.setFont (fonts::mono (11.5f));
                g.drawFittedText (path, r.withTrimmedRight (100), juce::Justification::centredLeft, 1, 0.8f);
            }
        }

        // Avvisi ed errori a sinistra dei pulsanti
        juce::String hint = resultText;

        if (hint.isEmpty() && settings.stems)
            hint = "Un file per ogni traccia con clip, allineati all'inizio del brano";

        g.setColour (resultText.isNotEmpty() ? colours::record : colours::textDim);
        g.setFont (fonts::ui (11.5f));
        g.drawFittedText (hint, footer.withTrimmedRight (220), juce::Justification::centredLeft, 2);
        return;
    }

    // Avanzamento o risultato
    auto body = panel.reduced (0, 12);

    if (stage == Stage::running)
    {
        const float progress = juce::jlimit (0.0f, 1.0f, exporter.getProgress());
        auto name = exporter.getCurrentJobName();
        g.setColour (colours::textMid);
        g.setFont (fonts::ui (12.5f));
        g.drawText ((settings.stems && name.isNotEmpty() ? "Traccia: " + name : juce::String ("Render del mix"))
                        + "  " + juce::String (juce::roundToInt (progress * 100.0f)) + "%",
                    body.removeFromTop (24), juce::Justification::centredLeft);

        auto bar = body.removeFromTop (10).toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (bar, 5.0f);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (10.0f, bar.getWidth() * progress)), 5.0f);
    }
    else
    {
        const bool ok = ! resultFiles.isEmpty();
        g.setColour (ok ? colours::play : colours::record);
        g.setFont (fonts::ui (13.5f, fonts::Weight::medium));
        g.drawText (resultText, body.removeFromTop (24), juce::Justification::centredLeft, true);

        if (ok)
        {
            g.setColour (colours::textDim);
            g.setFont (fonts::mono (11.5f));
            auto folder = resultFiles.getFirst().getParentDirectory().getFullPathName();
            const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();

            if (folder.startsWith (home))
                folder = "~" + folder.substring (home.length());

            g.drawText (folder, body.removeFromTop (24),
                        juce::Justification::centredLeft, true);
        }
    }
}

void ExportPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panelArea().contains (e.getPosition()) && stage != Stage::running)
        dismiss();
}

bool ExportPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        dismiss();
        return true;
    }

    if (key == juce::KeyPress::returnKey && stage == Stage::settings)
    {
        startExport();
        return true;
    }

    return true;   // il pannello e' modale: i tasti non passano sotto
}

} // namespace sonora
