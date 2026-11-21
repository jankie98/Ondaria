/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "MainComponent.h"
#include "OndariaIcons.h"
#include "SonoraBehaviour.h"
#include "Settings.h"
#include "Theme.h"
#include "host/IsolatedPluginScanner.h"
#include "host/PluginFormats.h"

namespace te = tracktion;

namespace sonora
{

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, te::Engine& engine)
        : DocumentWindow (name, colours::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new MainComponent (engine), true);
        setResizable (true, true);
        setResizeLimits (1100, 680, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);

        // Icona nella barra delle applicazioni e nel selettore delle finestre
        // (su Windows c'è già quella dell'eseguibile, su Linux serve qui).
        if (auto* peer = getPeer())
            peer->setIcon (juce::ImageCache::getFromMemory (OndariaIcons::ondaria512_png,
                                                            OndariaIcons::ondaria512_pngSize));
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class SonoraApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "Ondaria"; }
    const juce::String getApplicationVersion() override    { return SONORA_VERSION; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<te::Engine> ("Ondaria",
                                               std::make_unique<SonoraUIBehaviour>(),
                                               std::make_unique<SonoraEngineBehaviour>());

        // Host plugin: CLAP accanto ai formati di JUCE, scansione isolata.
        auto& pm = engine->getPluginManager();
        addAllPluginFormats (pm.pluginFormatManager);
        pm.knownPluginList.setCustomScanner (std::make_unique<IsolatedPluginScanner>());

        // Il tema scelto l'ultima volta, prima di creare la finestra
        if (auto settings = openUiSettings (*engine))
        {
            theme::apply (theme::load (*settings));
            applyUiScale (loadUiScale (*settings));
        }

        mainWindow = std::make_unique<MainWindow> ("Ondaria " + getApplicationVersion(), *engine);
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (mainWindow != nullptr)
            if (auto mc = dynamic_cast<MainComponent*> (mainWindow->getContentComponent()))
                mc->saveIfChanged();

        quit();
    }

private:
    SonoraLookAndFeel lookAndFeel;
    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace sonora

START_JUCE_APPLICATION (sonora::SonoraApplication)
