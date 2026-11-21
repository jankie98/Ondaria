/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

namespace sonora::tasks
{

/** Esegue un lavoro lungo del motore (render del freeze, per esempio) su un thread
    a parte e intanto fa girare il ciclo dei messaggi, cosi' l'interfaccia resta viva.
    Ritorna quando il lavoro e' finito. Va chiamata dal thread dei messaggi.
*/
inline void runWithMessageLoop (tracktion::ThreadPoolJobWithProgress& job)
{
    struct Runner final : public juce::Thread
    {
        explicit Runner (tracktion::ThreadPoolJobWithProgress& j) : juce::Thread (j.getJobName()), task (j)
        {
            startThread();
        }

        ~Runner() override
        {
            task.signalJobShouldExit();
            stopThread (10000);
        }

        void run() override
        {
            while (! threadShouldExit())
                if (task.runJob() == juce::ThreadPoolJob::jobHasFinished)
                    break;
        }

        tracktion::ThreadPoolJobWithProgress& task;
    };

    Runner runner (job);

    while (runner.isThreadRunning())
        if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (10))
            break;
}

/** Comportamento minimo per i test e gli strumenti senza interfaccia. */
struct HeadlessUIBehaviour : public tracktion::UIBehaviour
{
    void runTaskWithProgressBar (tracktion::ThreadPoolJobWithProgress& job) override   { runWithMessageLoop (job); }
};

} // namespace sonora::tasks
