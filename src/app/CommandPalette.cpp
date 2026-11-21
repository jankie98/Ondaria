/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "CommandPalette.h"
#include "Animation.h"
#include "Theme.h"
#include "Widgets.h"

namespace sonora
{

CommandPalette::CommandPalette()
{
    input.setFont (fonts::ui (15.0f));
    input.setTextToShowWhenEmpty ("Scrivi un comando o il nome di un device", colours::textDim);
    input.setIndents (40, 10);
    input.setColour (juce::TextEditor::backgroundColourId, colours::panelLight);
    input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    input.onTextChange = [this] { filter(); };
    input.onReturnKey = [this] { run (list.getSelectedRow()); };
    input.onEscapeKey = [this] { dismiss(); };
    input.addKeyListener (this);
    addAndMakeVisible (input);

    list.setModel (this);
    list.setRowHeight (34);
    list.setColour (juce::ListBox::backgroundColourId, colours::panelLight);
    addAndMakeVisible (list);

}

void CommandPalette::show (std::vector<Command> c)
{
    commands = std::move (c);
    input.clear();
    filter();
    anim::fadeIn (*this, 120);
    toFront (true);
    input.grabKeyboardFocus();
}

void CommandPalette::dismiss()
{
    setVisible (false);
}

int CommandPalette::matchScore (const juce::String& query, const juce::String& text)
{
    if (query.isEmpty())
        return 1;

    const auto q = query.toLowerCase(), t = text.toLowerCase();

    if (t.startsWith (q))
        return 1000 - t.length();

    if (auto index = t.indexOf (q); index >= 0)
        return 800 - index;

    // Sottosequenza: "nvtr" trova "Nuova traccia".
    int ti = 0, score = 0;

    for (auto ch : q)
    {
        if (ch == ' ')
            continue;

        const int found = t.indexOfChar (ti, ch);

        if (found < 0)
            return 0;

        score += found == ti ? 3 : 1;
        ti = found + 1;
    }

    return score;
}

void CommandPalette::filter()
{
    const auto q = input.getText().trim();
    std::vector<std::pair<int, size_t>> scored;

    for (size_t i = 0; i < commands.size(); ++i)
        if (auto s = juce::jmax (matchScore (q, commands[i].name), matchScore (q, commands[i].detail) / 2); s > 0)
            scored.push_back ({ s, i });

    std::stable_sort (scored.begin(), scored.end(), [] (auto& a, auto& b) { return a.first > b.first; });

    visible.clear();

    for (auto& [score, index] : scored)
        if (visible.size() < 200)
            visible.push_back (commands[index]);

    list.updateContent();
    list.selectRow (0);
    resized();
    repaint();
}

void CommandPalette::run (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;

    auto action = visible[(size_t) row].action;
    dismiss();

    if (action)
        juce::MessageManager::callAsync (action);
}

juce::Rectangle<int> CommandPalette::panelArea() const
{
    auto r = getLocalBounds();
    const int w = juce::jmin (560, r.getWidth() - 32);
    const int rows = juce::jlimit (1, 9, (int) visible.size());
    return { r.getCentreX() - w / 2, juce::jmin (120, r.getHeight() / 6), w, 56 + rows * 34 + 10 };
}

void CommandPalette::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.35f));

    auto panel = panelArea().toFloat();
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 24, { 0, 8 }).drawForRectangle (g, panel.toNearestInt());
    g.setColour (colours::panelLight);
    g.fillRoundedRectangle (panel, 12.0f);

    g.setColour (colours::outline);
    g.fillRect (panel.getX(), (float) input.getBottom() + 4.0f, panel.getWidth(), 1.0f);
}

void CommandPalette::paintOverChildren (juce::Graphics& g)
{
    icons::draw (g, icons::search(), input.getBounds().removeFromLeft (40).withSizeKeepingCentre (15, 15).toFloat(),
                 colours::textDim);
}

void CommandPalette::resized()
{
    auto r = panelArea().reduced (8);
    input.setBounds (r.removeFromTop (40));
    r.removeFromTop (8);
    list.setBounds (r);
}

void CommandPalette::mouseDown (const juce::MouseEvent& e)
{
    if (! panelArea().contains (e.getPosition()))
        dismiss();
}

bool CommandPalette::keyPressed (const juce::KeyPress& key)
{
    const int row = list.getSelectedRow();

    if (key == juce::KeyPress::downKey)     { list.selectRow (juce::jmin (row + 1, (int) visible.size() - 1)); return true; }
    if (key == juce::KeyPress::upKey)       { list.selectRow (juce::jmax (row - 1, 0)); return true; }
    if (key == juce::KeyPress::escapeKey)   { dismiss(); return true; }
    if (key == juce::KeyPress::returnKey)   { run (row); return true; }

    return false;
}

int CommandPalette::getNumRows()
{
    return (int) visible.size();
}

void CommandPalette::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) visible.size()))
        return;

    const auto& c = visible[(size_t) row];
    auto r = juce::Rectangle<int> (width, height);

    if (selected)
    {
        g.setColour (colours::hover);
        g.fillRoundedRectangle (r.toFloat(), metrics::radius);
    }

    r.reduce (12, 0);

    if (c.shortcut.isNotEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::mono (11.0f));
        g.drawText (c.shortcut, r.removeFromRight (110), juce::Justification::centredRight);
    }
    else if (c.detail.isNotEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::mono (10.5f));
        g.drawText (c.detail, r.removeFromRight (110), juce::Justification::centredRight);
    }

    g.setColour (colours::text);
    g.setFont (fonts::ui (13.5f));
    g.drawText (c.name, r, juce::Justification::centredLeft, true);
}

void CommandPalette::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    run (row);
}

void CommandPalette::returnKeyPressed (int row)
{
    run (row);
}

} // namespace sonora
