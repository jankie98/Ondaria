/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include "Theme.h"
#include "session/Tasks.h"

namespace sonora
{

/** Velo sopra la finestra mentre il motore fa un lavoro lungo (il render del freeze):
    mostra nome e avanzamento e blocca mouse e tastiera finche' non ha finito.
*/
class TaskOverlay final : public juce::Component,
                          private juce::Timer
{
public:
    explicit TaskOverlay (tracktion::ThreadPoolJobWithProgress& j) : job (j)
    {
        setWantsKeyboardFocus (true);
        setInterceptsMouseClicks (true, true);
        startTimerHz (30);
    }

    /** Fa girare il lavoro con il velo sopra la finestra attiva. */
    static void run (tracktion::ThreadPoolJobWithProgress& job)
    {
        juce::Component* host = nullptr;

        if (auto w = juce::TopLevelWindow::getActiveTopLevelWindow())
            host = w;
        else if (juce::Desktop::getInstance().getNumComponents() > 0)
            host = juce::Desktop::getInstance().getComponent (0);

        if (auto dw = dynamic_cast<juce::DocumentWindow*> (host))
            if (auto content = dw->getContentComponent())
                host = content;

        std::unique_ptr<TaskOverlay> overlay;

        if (host != nullptr)
        {
            overlay = std::make_unique<TaskOverlay> (job);
            host->addAndMakeVisible (*overlay);
            overlay->setBounds (host->getLocalBounds());
            overlay->grabKeyboardFocus();
        }

        tasks::runWithMessageLoop (job);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.45f));

        auto card = getLocalBounds().withSizeKeepingCentre (380, 96).toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (colours::outline);
        g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

        auto inner = card.reduced (20.0f, 18.0f);
        g.setColour (colours::text);
        g.setFont (fonts::ui (14.0f, fonts::Weight::semibold));
        g.drawText (describe (job.getJobName()), inner.removeFromTop (20.0f), juce::Justification::centredLeft, true);

        const auto progress = juce::jlimit (0.0f, 1.0f, job.getCurrentTaskProgress());
        g.setColour (colours::textDim);
        g.setFont (fonts::mono (11.5f));
        g.drawText (juce::String (juce::roundToInt (progress * 100.0f)) + "%", inner.removeFromTop (18.0f),
                    juce::Justification::centredLeft, false);

        auto bar = inner.removeFromBottom (6.0f);
        g.setColour (colours::panelLight);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colours::accent);
        g.fillRoundedRectangle (bar.withWidth (std::max (6.0f, bar.getWidth() * progress)), 3.0f);
    }

    bool keyPressed (const juce::KeyPress&) override    { return true; }

private:
    /** I nomi dei lavori del motore sono in inglese. */
    static juce::String describe (const juce::String& name)
    {
        if (name.startsWith ("Creating track freeze for"))
            return "Congelo " + name.fromFirstOccurrenceOf ("for ", false, false).upToLastOccurrenceOf ("...", false, false) + "...";

        return name;
    }

    void timerCallback() override
    {
        if (auto p = getParentComponent(); p != nullptr && getBounds() != p->getLocalBounds())
            setBounds (p->getLocalBounds());

        repaint();
    }

    tracktion::ThreadPoolJobWithProgress& job;
};

} // namespace sonora
