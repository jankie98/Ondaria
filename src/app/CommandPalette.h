/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace sonora
{

/** Palette comandi (Ctrl+K): cerca un'azione o un device scrivendone il nome. */
class CommandPalette final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::KeyListener
{
public:
    struct Command
    {
        juce::String name, detail, shortcut;
        std::function<void()> action;
    };

    CommandPalette();

    /** Mostra la palette sopra il componente padre con questi comandi. */
    void show (std::vector<Command>);
    void dismiss();

    /** Punteggio di corrispondenza: lettere della ricerca in ordine nel nome (0 = nessuna). */
    static int matchScore (const juce::String& query, const juce::String& text);

    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    bool keyPressed (const juce::KeyPress& k, juce::Component*) override   { return keyPressed (k); }

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    void filter();
    void run (int row);
    juce::Rectangle<int> panelArea() const;

    juce::TextEditor input;
    juce::ListBox list;
    std::vector<Command> commands, visible;
};

} // namespace sonora
