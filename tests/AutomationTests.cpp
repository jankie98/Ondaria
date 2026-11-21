/*
    Sonora - test delle automazioni.

    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include <tracktion_engine/tracktion_engine.h>

#include "session/Automation.h"
#include "session/Session.h"
#include "HostedPlayer.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace
{

using sonora::test::HostedPlayer;
namespace au = sonora::automation;

struct AutomationTests final : public juce::UnitTest
{
    AutomationTests() : UnitTest ("Automazioni", "Sonora") {}

    void runTest() override
    {
        te::Engine engine ("SonoraAutomationTests", nullptr, nullptr);
        HostedPlayer player (engine);

        auto edit = te::createEmptyEdit (engine, juce::File::createTempFile (".tracktionedit"));
        edit->ensureNumberOfAudioTracks (1);
        sonora::session::ensureGrid (*edit);
        au::prepareEdit (*edit);
        edit->getTransport().ensureContextAllocated();
        edit->dispatchPendingUpdatesSynchronously();
        player.pump();

        auto& track = *te::getAudioTracks (*edit)[0];
        auto& transport = edit->getTransport();

        beginTest ("Parametri: mixer prima, poi i device");
        {
            auto params = au::getParameters (track);
            expect (params.size() >= 2);
            expect (params[0].name == "Mixer: Volume", params[0].name);
            expect (params[1].name.startsWith ("Mixer: Pan"), params[1].name);

            auto eq = edit->getPluginCache().createNewPlugin (te::EqualiserPlugin::xmlTypeName, {});
            track.pluginList.insertPlugin (eq, 0, nullptr);
            params = au::getParameters (track);
            const bool hasEq = std::any_of (params.begin(), params.end(), [] (auto& p) { return p.name.startsWith ("4-Band Equaliser:"); });
            expect (hasEq);

            // La chiave ritrova lo stesso parametro
            for (auto& p : params)
                expect (au::findParameter (track, p.key) == p.param);
        }

        auto volume = track.getVolumePlugin()->volParam;
        const auto volumeKey = au::keyFor (*volume);

        beginTest ("Corsie mostrate");
        {
            expect (! au::isLaneShown (track, volumeKey));
            au::setLaneShown (track, volumeKey, true);
            au::setLaneShown (track, "x/y", true);
            expect (au::isLaneShown (track, volumeKey));
            expect (au::getShownLanes (track) == juce::StringArray (volumeKey, juce::String ("x/y")));
            au::setLaneShown (track, "x/y", false);
            expect (au::getShownLanes (track) == juce::StringArray (volumeKey));
        }

        beginTest ("Punti: aggiungere, spostare, togliere, annullare");
        {
            auto& um = edit->getUndoManager();
            um.beginNewTransaction();
            au::addPoint (*volume, 1_tp, 0.2f);
            au::addPoint (*volume, 3_tp, 0.8f);
            expectEquals (volume->getCurve().getNumPoints(), 2);
            expectWithinAbsoluteError (au::normalisedValueAt (*volume, 2_tp), 0.5f, 0.01f);
            expectWithinAbsoluteError (au::normalisedValueAt (*volume, 0_tp), 0.2f, 0.01f);

            um.beginNewTransaction();
            const int index = au::movePoint (*volume, 1, 4_tp, 1.0f);
            expectEquals (index, 1);
            expectWithinAbsoluteError (volume->getCurve().getPointTime (1).inSeconds(), 4.0, 0.001);
            expectWithinAbsoluteError (au::normalisedValueAt (*volume, 4_tp), 1.0f, 0.01f);

            // Un punto non scavalca i vicini: resta tra loro
            um.beginNewTransaction();
            expectEquals (au::movePoint (*volume, 1, 0.5_tp, 0.0f), 1);
            expectWithinAbsoluteError (volume->getCurve().getPointTime (1).inSeconds(), 1.0, 0.001);

            um.beginNewTransaction();
            au::removePoint (*volume, 0);
            expectEquals (volume->getCurve().getNumPoints(), 1);

            edit->undo();
            expectEquals (volume->getCurve().getNumPoints(), 2);
            edit->undo();
            edit->undo();
            expectWithinAbsoluteError (volume->getCurve().getPointTime (1).inSeconds(), 3.0, 0.001);
        }

        beginTest ("La riproduzione segue la curva");
        {
            au::clear (*volume);
            au::addPoint (*volume, 0_tp, 0.0f);
            au::addPoint (*volume, 2_tp, 1.0f);

            transport.setPosition (0_tp);
            transport.play (false);
            player.process (1.0);
            player.pump();

            expectWithinAbsoluteError (au::toNormalised (*volume, volume->getCurrentValue()), 0.5f, 0.05f);

            transport.stop (false, false);
            player.process (0.1);
        }

        beginTest ("Scrittura: i movimenti mentre il brano suona diventano punti");
        {
            auto pan = track.getVolumePlugin()->panParam;
            au::clear (*pan);
            expect (! au::isWriting (*edit));

            // Spenta: muovere il pan non scrive nulla
            transport.setPosition (0_tp);
            transport.play (false);
            player.process (0.2);
            pan->parameterChangeGestureBegin();
            pan->setParameter (0.5f, juce::sendNotification);
            pan->parameterChangeGestureEnd();
            player.process (0.2);
            transport.stop (false, false);
            player.process (0.1);
            player.pump (100);
            expectEquals (pan->getCurve().getNumPoints(), 0);

            // Accesa: scrive
            au::setWriting (*edit, true);
            transport.setPosition (0_tp);
            transport.play (false);
            player.process (0.3);
            pan->parameterChangeGestureBegin();

            for (int i = 0; i <= 10; ++i)
            {
                pan->setParameter (-1.0f + 0.2f * (float) i, juce::sendNotification);
                player.process (0.1);
                player.pump (5);

                if (i == 5)
                    expect (au::showLanesBeingWritten (*edit));
            }

            pan->parameterChangeGestureEnd();
            player.process (0.3);
            transport.stop (false, false);
            player.process (0.1);
            player.pump (100);

            expect (pan->getCurve().getNumPoints() >= 3, "punti scritti: " + juce::String (pan->getCurve().getNumPoints()));
            expect (au::isLaneShown (track, au::keyFor (*pan)));

            // Dove si e' mosso, la curva sale da sinistra a destra
            expect (au::normalisedValueAt (*pan, 0.5_tp) < au::normalisedValueAt (*pan, 1.2_tp));
            au::setWriting (*edit, false);
        }

        beginTest ("Mostrare e nascondere le corsie con automazione");
        {
            au::hideAllLanes (track);
            expect (au::getShownLanes (track).isEmpty());
            expect (au::hasAutomation (track));
            expectEquals (au::showAutomatedLanes (track), 2);   // volume e pan
            expect (au::isLaneShown (track, volumeKey));
            expectEquals (au::showAutomatedLanes (track), 0);

            au::clear (*volume);
            au::clear (*track.getVolumePlugin()->panParam);
            expect (! au::hasAutomation (track));
        }
    }
};

static AutomationTests automationTests;

} // namespace
