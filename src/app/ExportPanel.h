/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "Widgets.h"
#include "session/Export.h"

namespace sonora
{

/** Scelta fra poche opzioni affiancate (una sola accesa). */
class Segmented final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    void setOptions (juce::StringArray, int selected);
    void setSelected (int);
    int getSelected() const                        { return selected; }
    void setEnabled (int index, bool);

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override   { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

private:
    juce::Rectangle<float> optionArea (int) const;

    juce::StringArray options;
    juce::Array<bool> enabled;
    int selected = 0;
};

/** Interruttore con una scritta accanto. */
class Switch final : public juce::Component
{
public:
    explicit Switch (juce::String text) : label (std::move (text)) {}

    void setOn (bool b)                           { on = b; repaint(); }
    bool isOn() const                             { return on; }
    std::function<void()> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String label;
    bool on = false;
};

//==============================================================================
/** Pannello di export (Ctrl+Shift+R): mix o stem, formato, qualita', dove salvare.
    Mostra l'avanzamento mentre il render gira in sottofondo.
*/
class ExportPanel final : public juce::Component,
                          private juce::Timer
{
public:
    ExportPanel();
    ~ExportPanel() override;

    /** Apre il pannello per il progetto dato. */
    void show (tracktion::Edit&);
    void dismiss();

    /** Messaggi per la barra di stato. */
    std::function<void (const juce::String&)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum class Stage { settings, running, done };

    void timerCallback() override;
    void refresh();
    void chooseDestination();
    void startExport();
    void finished (juce::Result, juce::Array<juce::File>);
    juce::Rectangle<int> panelArea() const;
    juce::String rangeDescription() const;

    tracktion::Edit* edit = nullptr;
    exporting::Settings settings;
    exporting::Exporter exporter;
    bool destinationChosen = false;
    Stage stage = Stage::settings;
    juce::String resultText;
    juce::Array<juce::File> resultFiles;
    std::unique_ptr<juce::FileChooser> chooser;

    Segmented what, range, format, quality, rate;
    Switch normalise { "Normalizza (picco a -0.3 dB)" }, tails { "Lascia finire riverberi e delay" };
    FlatButton chooseButton { "Cambia...", 12.0f }, cancelButton { "Annulla", 12.5f },
               exportButton { "Esporta", 12.5f }, revealButton { "Mostra nella cartella", 12.5f };
};

} // namespace sonora
