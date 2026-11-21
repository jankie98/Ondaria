/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "SessionView.h"
#include "Animation.h"
#include "FreezeUI.h"
#include "RecordingUI.h"
#include "Theme.h"
#include "Widgets.h"
#include "session/Session.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    constexpr int sceneColumnWidth = 120;
    constexpr int columnGap = 6;
    constexpr int headerHeight = 30;
    constexpr int stopRowHeight = 22;
    constexpr int mixerHeight = 184;
    constexpr int padding = 14;
    constexpr int launchZone = 22;

    juce::Colour trackColour (te::Track& t)
    {
        auto c = t.getColour();
        return c.isTransparent() ? colours::accent : c;
    }

    juce::Colour clipColour (te::Clip& c)
    {
        // Senza un colore scelto, la clip prende quello della traccia.
        if (! c.state.hasProperty (te::IDs::colour))
            if (auto slot = c.getClipSlot())
                return trackColour (slot->track);

        return c.getColour();
    }

    juce::String sceneName (te::Scene& s, int index)
    {
        auto n = s.name.get();
        return n.isNotEmpty() ? n : "Scena " + juce::String (index + 1);
    }

    void showRenameBox (juce::Component& parent, juce::Rectangle<int> area, const juce::String& current,
                        std::function<void (const juce::String&)> onDone)
    {
        auto* ed = new juce::TextEditor();
        ed->setFont (fonts::ui (12.5f, fonts::Weight::medium));
        ed->setText (current, false);
        ed->selectAll();
        ed->setBounds (area);
        parent.addAndMakeVisible (ed);
        ed->grabKeyboardFocus();

        auto finish = [ed, onDone] (bool apply)
        {
            if (apply && ed->getText().trim().isNotEmpty())
                onDone (ed->getText().trim());

            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<juce::TextEditor> (ed)]
            {
                delete safe.getComponent();
            });
        };

        ed->onReturnKey = [finish] { finish (true); };
        ed->onEscapeKey = [finish] { finish (false); };
        ed->onFocusLost = [finish] { finish (true); };
    }

    juce::PopupMenu colourMenu (std::function<void (juce::Colour)> onPick)
    {
        juce::PopupMenu m;

        for (int i = 0; i < 16; ++i)
        {
            auto c = session::colourForIndex (i);
            juce::PopupMenu::Item item ("   ");
            item.colour = c;
            item.action = [onPick, c] { onPick (c); };

            juce::Image swatch (juce::Image::ARGB, 28, 14, true);
            juce::Graphics g (swatch);
            g.setColour (c);
            g.fillRoundedRectangle (0.0f, 0.0f, 28.0f, 14.0f, 3.0f);
            item.image = std::make_unique<juce::DrawableImage> (swatch);
            m.addItem (std::move (item));
        }

        return m;
    }
}

//==============================================================================
class SessionView::SlotComponent final : public juce::Component,
                                         public HintProvider,
                                         public juce::FileDragAndDropTarget,
                                         public juce::DragAndDropTarget
{
public:
    SlotComponent (SessionView& v, te::AudioTrack& t, int index)
        : view (v), track (t), sceneIndex (index)
    {
    }

    te::ClipSlot* getSlot() const       { return session::getSlot (track, sceneIndex); }

    juce::String getHintAt (juce::Point<int> pos) override
    {
        auto slot = getSlot();

        if (slot == nullptr)
            return {};

        if (freeze::isFrozen (track))
            return freezeui::frozenNotice (track);

        const bool inLaunch = pos.x < launchZone;
        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");

        if (view.recorder != nullptr && view.recorder->getSlotRecordState (*slot) != recording::Recorder::SlotRecordState::none)
            return "Registrazione in corso: clic sul cerchio per fermarla a tempo";

        if (slot->getClip() != nullptr)
            return inLaunch ? "Lancia la clip (Invio)"
                            : "Clip: doppio clic per lanciarla" + dot + "le note sono nel pannello sotto" + dot
                                + "Canc elimina" + dot + "clic destro per il menu";

        if (view.recorder != nullptr && view.recorder->canRecordInto (*slot))
            return inLaunch ? "Registra in questo slot, a tempo" : "Slot vuoto: il cerchio registra" + dot + "doppio clic per una clip MIDI";

        return inLaunch ? "Ferma la traccia"
                        : "Slot vuoto: doppio clic per una clip MIDI" + dot + "trascina qui un campione" + dot + "frecce per muoverti";
    }

    void paint (juce::Graphics& g) override
    {
        paintSlot (g);

        if (auto slot = getSlot(); slot != nullptr && slot->getClip() != nullptr && freeze::isFrozen (track))
            freezeui::drawFrozenVeil (g, getLocalBounds().toFloat(), metrics::radius);
    }

    void paintSlot (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        auto slot = getSlot();
        auto clip = slot != nullptr ? slot->getClip() : nullptr;
        const bool selected = view.getSelectedSlot() == slot && slot != nullptr;

        auto recState = recording::Recorder::SlotRecordState::none;

        if (view.recorder != nullptr && slot != nullptr)
            recState = view.recorder->getSlotRecordState (*slot);

        if (clip == nullptr && recState != recording::Recorder::SlotRecordState::none)
        {
            // Registrazione in corso: lampeggia quando aspetta di fermarsi a tempo.
            const bool stopping = recState == recording::Recorder::SlotRecordState::stopping;
            const bool on = ! stopping || (juce::Time::getMillisecondCounter() / 250) % 2 == 0;

            g.setColour (colours::record.withAlpha (0.28f));
            g.fillRoundedRectangle (r, metrics::radius);

            g.setColour (colours::record.withAlpha (on ? 1.0f : 0.35f));
            g.fillEllipse (r.removeFromLeft ((float) launchZone).withSizeKeepingCentre (10.0f, 10.0f));

            // Quello che sta arrivando, compresso nello slot dall'inizio a adesso
            if (auto take = view.recorder->getLiveTake (track))
            {
                const auto now = track.edit.getTransport().getPosition();
                const auto body = r.reduced (2.0f, 4.0f);
                const auto length = std::max (0.25, (now - take->start).inSeconds());

                recordingui::drawLiveTake (g, *take, body, now, [body, take, length] (te::TimePosition time)
                {
                    return body.getX() + body.getWidth() * (float) ((time - take->start).inSeconds() / length);
                }, colours::record.brighter (0.2f).withAlpha (0.7f));
            }

            g.setColour (colours::text);
            g.setFont (fonts::ui (11.5f, fonts::Weight::medium));
            g.drawText (stopping ? "Chiusura..." : "Registrazione", r.reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
        }
        else if (clip == nullptr)
        {
            g.setColour (dragOver ? colours::hover : colours::emptySlot);
            g.fillRoundedRectangle (r, metrics::radius);

            const bool canRecord = view.recorder != nullptr && slot != nullptr && view.recorder->canRecordInto (*slot);
            auto zone = r.removeFromLeft ((float) launchZone);

            if (canRecord && ! dragOver)
            {
                // Traccia armata: ogni slot vuoto mostra il pulsante di registrazione.
                const bool hoverZone = isMouseOver() && getMouseXYRelative().x < launchZone;
                g.setColour (hoverZone ? colours::record : colours::record.withAlpha (0.6f));
                g.drawEllipse (zone.withSizeKeepingCentre (10.0f, 10.0f), 1.5f);
            }
            else if (isMouseOver() && ! dragOver)
            {
                // Quadratino di stop, come negli slot vuoti di un clip launcher.
                g.setColour (colours::textDim);
                g.fillRoundedRectangle (zone.withSizeKeepingCentre (7.0f, 7.0f), 1.0f);
            }
        }
        else
        {
            const auto state = session::getSlotState (*slot);
            const bool playing = state == session::SlotState::playing || state == session::SlotState::stopQueued;
            auto colour = clipColour (*clip);

            // Lampo quando la clip parte davvero (al punto di quantizzazione)
            const auto now = juce::Time::getMillisecondCounter();

            if (playing && ! wasPlaying)
                launchFlashStart = now;

            wasPlaying = playing;

            g.setColour (playing ? colour : colour.withMultipliedSaturation (0.85f).withMultipliedBrightness (0.85f));
            g.fillRoundedRectangle (r, metrics::radius);

            if (auto progress = session::getPlayProgress (*clip))
            {
                // Pulsazione morbida a ogni beat (non in modalita' performance: distrae)
                if (! performance::enabled)
                {
                    const double beats = *progress * clip->getLengthInBeats().inBeats();
                    const float phase = (float) (beats - std::floor (beats));
                    const float pulse = (1.0f - phase) * (1.0f - phase);

                    g.setColour (juce::Colours::white.withAlpha (0.10f * pulse));
                    g.fillRoundedRectangle (r, metrics::radius);
                }

                g.setColour (colours::ink.withAlpha (0.35f));
                g.fillRect (r.withTop (r.getBottom() - 3.0f).withWidth (r.getWidth() * *progress));
            }

            if (const auto elapsed = now - launchFlashStart; playing && elapsed < flashMs)
            {
                const float k = 1.0f - anim::easeOut ((float) elapsed / (float) flashMs);
                g.setColour (juce::Colours::white.withAlpha (0.45f * k));
                g.fillRoundedRectangle (r, metrics::radius);
                g.setColour (colour.brighter (0.8f).withAlpha (k));
                g.drawRoundedRectangle (r.reduced (0.75f), metrics::radius, 1.5f);
            }

            auto launch = r.removeFromLeft ((float) launchZone).withSizeKeepingCentre (9.0f, 10.0f);
            auto ink = colours::ink;

            if (state == session::SlotState::playQueued || state == session::SlotState::stopQueued)
            {
                // Lampeggia mentre attende il punto di quantizzazione.
                const bool on = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
                ink = on ? colours::ink : colours::ink.withAlpha (0.35f);
            }

            icons::draw (g, state == session::SlotState::stopQueued ? icons::stop() : icons::play(),
                         launch, playing || state == session::SlotState::playQueued ? ink : ink.withAlpha (0.55f));

            g.setColour (colours::ink);
            g.setFont (fonts::ui (performance::enabled ? 14.5f : 11.5f, fonts::Weight::medium));
            g.drawText (clip->getName(), r.reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
        }

        if (selected)
        {
            g.setColour (colours::text);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.75f), metrics::radius, 1.5f);
        }
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto slot = getSlot();

        if (slot == nullptr)
            return;

        view.selectSlot (&track, sceneIndex);

        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }

        if (e.x < launchZone)
        {
            auto rec = view.recorder;

            if (freeze::isFrozen (track))
            {
                view.status (freezeui::frozenNotice (track));
            }
            else if (auto clip = slot->getClip())
            {
                session::launchClip (*clip);
            }
            else if (rec != nullptr && rec->getSlotRecordState (*slot) == recording::Recorder::SlotRecordState::recording)
            {
                rec->stopSlotRecording (*slot);
            }
            else if (rec != nullptr && rec->canRecordInto (*slot))
            {
                if (auto res = rec->startSlotRecording (*slot); res.failed())
                    view.status (res.getErrorMessage());
                else
                    view.status ("Registrazione su " + track.getName() + ": clicca di nuovo per fermarla a tempo");
            }
            else
            {
                session::stopTrack (track);
            }
        }
    }

    void mouseMove (const juce::MouseEvent&) override   { repaint(); }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto slot = getSlot();

        if (slot == nullptr || e.x < launchZone)
            return;

        if (freeze::isFrozen (track))
        {
            view.status (freezeui::frozenNotice (track));
        }
        else if (slot->getClip() == nullptr)
        {
            session::createMidiClip (*slot, 1);
            view.status ("Creata una clip MIDI di una battuta su " + track.getName());
            view.selectSlot (&track, sceneIndex);
        }
        else
        {
            session::launchClip (*slot->getClip());
        }
    }

    void showMenu()
    {
        auto slot = getSlot();

        if (slot == nullptr)
            return;

        juce::PopupMenu m;
        juce::Component::SafePointer<SlotComponent> safe (this);

        if (auto clip = slot->getClip())
        {
            te::Clip::Ptr clipPtr (clip);
            m.addItem ("Lancia", [clipPtr] { session::launchClip (*clipPtr); });
            m.addItem ("Rinomina", [safe, clipPtr]
            {
                if (safe != nullptr)
                    showRenameBox (*safe, safe->getLocalBounds(), clipPtr->getName(), [clipPtr] (const juce::String& n)
                    {
                        clipPtr->edit.getUndoManager().beginNewTransaction ("Rinomina clip");
                        clipPtr->setName (n);
                    });
            });
            m.addSubMenu ("Colore", colourMenu ([clipPtr] (juce::Colour c)
            {
                clipPtr->edit.getUndoManager().beginNewTransaction ("Colore clip");
                clipPtr->setColour (c);
            }));
            m.addSeparator();
            m.addItem ("Elimina", [safe]
            {
                if (safe != nullptr)
                    if (auto s = safe->getSlot())
                    {
                        s->edit.getUndoManager().beginNewTransaction ("Elimina clip");
                        session::deleteClip (*s);
                    }
            });
        }
        else
        {
            for (int bars : { 1, 2, 4, 8 })
                m.addItem ("Crea clip MIDI (" + juce::String (bars) + (bars == 1 ? " battuta)" : " battute)"), [safe, bars]
                {
                    if (safe != nullptr)
                        if (auto s = safe->getSlot())
                        {
                            session::createMidiClip (*s, bars);
                            safe->view.selectSlot (&safe->track, safe->sceneIndex);
                        }
                });
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    // Trascinamento di file dal sistema
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return isSupported (juce::File (files[0]));
    }

    void fileDragEnter (const juce::StringArray&, int, int) override   { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragOver = false;
        dropFile (juce::File (files[0]));
    }

    // Trascinamento dal browser di Sonora
    bool isInterestedInDragSource (const SourceDetails& d) override
    {
        return d.description.toString().startsWith ("file:");
    }

    void itemDragEnter (const SourceDetails&) override     { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override      { dragOver = false; repaint(); }

    void itemDropped (const SourceDetails& d) override
    {
        dragOver = false;
        dropFile (juce::File (d.description.toString().fromFirstOccurrenceOf ("file:", false, false)));
    }

private:
    static constexpr juce::uint32 flashMs = 450;
    bool wasPlaying = false;
    juce::uint32 launchFlashStart = 0;

    static bool isSupported (const juce::File& f)
    {
        return f.hasFileExtension (te::soundFileExtensions) || f.hasFileExtension (te::midiFileExtensions);
    }

    void dropFile (const juce::File& f)
    {
        repaint();

        if (auto slot = getSlot())
        {
            if (session::createClipFromFile (*slot, f) != nullptr)
                view.status ("Importato " + f.getFileName() + " su " + track.getName());
            else
                view.status ("Formato non supportato: " + f.getFileName());

            view.selectSlot (&track, sceneIndex);
        }
    }

    SessionView& view;
    te::AudioTrack& track;
    const int sceneIndex;
    bool dragOver = false;
};

//==============================================================================
class SessionView::TrackColumn final : public juce::Component,
                                       public HintProvider
{
public:
    TrackColumn (SessionView& v, te::AudioTrack& t)
        : view (v), track (&t)
    {
        const int numScenes = t.edit.getSceneList().getNumScenes();

        for (int i = 0; i < numScenes; ++i)
            addAndMakeVisible (slots.add (new SlotComponent (v, t, i)));

        stopButton.setIconColour (colours::textDim);
        stopButton.setDrawsBackground (true);
        stopButton.onClick = [this] { session::stopTrack (*track); };
        addAndMakeVisible (stopButton);

        volume.setSliderStyle (juce::Slider::LinearVertical);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setColour (juce::Slider::trackColourId, colours::hover.brighter (0.15f));
        volume.onDragStart = [this] { track->edit.getUndoManager().beginNewTransaction ("Volume"); };
        volume.onValueChange = [this]
        {
            if (auto vp = track->getVolumePlugin())
                vp->setVolumeDb ((float) volume.getValue());

            updateDbLabel();
        };
        addAndMakeVisible (volume);

        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        pan.setColour (juce::Slider::rotarySliderFillColourId, trackColour (t));
        pan.setTooltip ("Pan");
        pan.onDragStart = [this] { track->edit.getUndoManager().beginNewTransaction ("Pan"); };
        pan.onValueChange = [this]
        {
            if (auto vp = track->getVolumePlugin())
                vp->setPan ((float) pan.getValue());
        };
        addAndMakeVisible (pan);

        for (auto* b : { &muteButton, &soloButton })
        {
            b->setBaseColour (colours::panelLight);
            b->setClickingTogglesState (false);
            addAndMakeVisible (b);
        }

        muteButton.setTooltip ("Silenzia la traccia");
        soloButton.setTooltip ("Ascolta solo questa traccia");
        muteButton.setOnColours (colours::warning, colours::ink);
        soloButton.setOnColours (colours::accent, colours::ink);
        muteButton.onClick = [this] { track->setMute (! track->isMuted (false)); refresh(); };
        soloButton.onClick = [this] { track->setSolo (! track->isSolo (false)); refresh(); };

        armButton.setClickingTogglesState (false);
        armButton.onClick = [this]
        {
            if (auto rec = view.recorder)
            {
                rec->setArmed (*track, ! recording::isExplicitlyArmed (*track));
                refresh();
            }
        };
        addAndMakeVisible (armButton);

        dbLabel.setFont (fonts::mono (11.0f));
        dbLabel.setJustificationType (juce::Justification::centred);
        dbLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (dbLabel);

        addAndMakeVisible (meter);

        if (auto lm = t.getLevelMeterPlugin())
            meter.setMeasurer (&lm->measurer);

        refresh();
    }

    ~TrackColumn() override
    {
        meter.setMeasurer (nullptr);
    }

    te::AudioTrack& getTrack()      { return *track; }

    /** Allinea i controlli al modello (undo, automazioni, altre viste). */
    void refresh()
    {
        dbLabel.setColour (juce::Label::textColourId, colours::textMid);   // segue il tema

        if (auto vp = track->getVolumePlugin())
        {
            if (! volume.isMouseButtonDown())
                volume.setValue (vp->getVolumeDb(), juce::dontSendNotification);

            if (! pan.isMouseButtonDown())
                pan.setValue (vp->getPan(), juce::dontSendNotification);
        }

        muteButton.setToggleState (track->isMuted (false), juce::dontSendNotification);
        soloButton.setToggleState (track->isSolo (false), juce::dontSendNotification);

        if (auto rec = view.recorder)
        {
            const bool armed = rec->isArmed (*track);
            const bool autoArmed = armed && ! recording::isExplicitlyArmed (*track);

            if (armButton.getToggleState() != armed || armButton.autoArmed != autoArmed)
            {
                armButton.autoArmed = autoArmed;
                armButton.setToggleState (armed, juce::dontSendNotification);
                armButton.repaint();
            }

            armButton.setTooltip (recordingui::armTooltip (*rec, *track));
        }

        armButton.setVisible (view.recorder != nullptr);
        const auto colour = trackColour (*track);
        pan.setColour (juce::Slider::rotarySliderFillColourId, colour);
        volume.setColour (juce::Slider::trackColourId, colour.withAlpha (0.75f));
        updateDbLabel();
    }

    void repaintSlots()
    {
        for (auto* s : slots)
            s->repaint();

        repaint (headerArea());
    }

    void paint (juce::Graphics& g) override
    {
        auto header = headerArea().toFloat();
        const bool selected = view.getSelectedTrack() == track.get();

        const auto colour = trackColour (*track);

        // Intestazione tinta col colore della traccia, con una fascia piena in alto
        g.setColour (selected ? colours::selection : colours::panel);
        g.fillRoundedRectangle (header, metrics::radius);
        g.setColour (colour.withAlpha (selected ? 0.22f : 0.12f));
        g.fillRoundedRectangle (header, metrics::radius);

        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (header.toNearestInt().removeFromTop (3));
            g.setColour (colour);
            g.fillRoundedRectangle (header, metrics::radius);
        }

        if (selected)
        {
            g.setColour (colour.brighter (0.3f));
            g.drawRoundedRectangle (header.reduced (0.5f), metrics::radius, 1.0f);
        }

        auto text = header.reduced (10.0f, 0.0f).withTrimmedTop (2.0f);
        g.setColour (colours::text);
        g.setFont (fonts::ui (12.5f, fonts::Weight::medium));
        if (freeze::isFrozen (*track))
        {
            auto flake = header.withTrimmedRight (6.0f).removeFromRight (20.0f);
            freezeui::drawSnowflake (g, flake.withSizeKeepingCentre (13.0f, 13.0f), freezeui::iceColour());
            text = text.withTrimmedRight (22.0f);
        }

        g.drawText (track->getName(), text, juce::Justification::centredLeft, true);

        // Canale del mixer con lo stesso colore in alto
        auto mixer = mixerArea().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (mixer, 8.0f);

        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (mixer.toNearestInt().removeFromTop (3));
            g.setColour (colour.withAlpha (0.9f));
            g.fillRoundedRectangle (mixer, 8.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (headerHeight + metrics::gap);

        for (auto* s : slots)
        {
            s->setBounds (r.removeFromTop (performance::slotHeight()));
            r.removeFromTop (metrics::gap);
        }

        stopButton.setBounds (r.removeFromTop (stopRowHeight).withSizeKeepingCentre (32, stopRowHeight));

        auto mixer = mixerArea().reduced (10);
        auto buttons = mixer.removeFromBottom (22);
        const int third = buttons.getWidth() / 3;
        muteButton.setBounds (buttons.removeFromLeft (third).reduced (2, 0));
        soloButton.setBounds (buttons.removeFromLeft (third).reduced (2, 0));
        armButton.setBounds (buttons.reduced (2, 0));
        mixer.removeFromBottom (6);
        dbLabel.setBounds (mixer.removeFromBottom (16));
        mixer.removeFromBottom (4);

        auto top = mixer.removeFromTop (34);
        pan.setBounds (top.withSizeKeepingCentre (34, 34));
        mixer.removeFromTop (6);

        auto faders = mixer.withSizeKeepingCentre (56, mixer.getHeight());
        meter.setBounds (faders.removeFromLeft (14));
        faders.removeFromLeft (12);
        volume.setBounds (faders.removeFromLeft (24));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! headerArea().contains (e.getPosition()))
            return;

        view.selectTrack (track.get());

        if (e.mods.isPopupMenu())
            showHeaderMenu();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (headerArea().contains (e.getPosition()))
            rename();
    }

    juce::String getHintAt (juce::Point<int> pos) override
    {
        if (headerArea().contains (pos))
            return juce::String::fromUTF8 ("Traccia: doppio clic per rinominarla \xc2\xb7 clic destro per colore, ingresso, congela");

        return {};
    }

    static int requiredHeight (int numScenes)
    {
        return headerHeight + metrics::gap + numScenes * (performance::slotHeight() + metrics::gap)
                + stopRowHeight + metrics::gap * 2 + mixerHeight;
    }

private:
    juce::Rectangle<int> headerArea() const     { return getLocalBounds().removeFromTop (headerHeight); }
    juce::Rectangle<int> mixerArea() const      { return getLocalBounds().removeFromBottom (mixerHeight); }

    void updateDbLabel()
    {
        const auto db = volume.getValue();
        dbLabel.setText (db <= -59.95 ? "-inf" : juce::String (db, 1) + " dB", juce::dontSendNotification);
    }

    void rename()
    {
        showRenameBox (*this, headerArea().reduced (2), track->getName(), [t = track] (const juce::String& n)
        {
            t->edit.getUndoManager().beginNewTransaction ("Rinomina traccia");
            t->setName (n);
        });
    }

    void showHeaderMenu()
    {
        juce::PopupMenu m;
        juce::Component::SafePointer<TrackColumn> safe (this);
        te::AudioTrack::Ptr t (track);

        m.addItem ("Rinomina", [safe] { if (safe != nullptr) safe->rename(); });
        m.addSubMenu ("Colore", colourMenu ([t] (juce::Colour c)
        {
            t->edit.getUndoManager().beginNewTransaction ("Colore traccia");
            t->setColour (c);
        }));

        if (auto rec = view.recorder)
        {
            m.addSubMenu ("Ingresso", recordingui::inputMenu (*rec, *t));
            m.addItem ("Arma per la registrazione", true, recording::isExplicitlyArmed (*t),
                       [t, r = juce::WeakReference<recording::Recorder> (rec)]
                       {
                           if (r != nullptr)
                               r->setArmed (*t, ! recording::isExplicitlyArmed (*t));
                       });
        }

        m.addSeparator();
        freezeui::addMenuItems (m, *t, [safe] (const juce::String& s)
        {
            if (safe != nullptr)
            {
                safe->repaint();
                safe->view.status (s);
            }
        });
        m.addSeparator();
        m.addItem ("Elimina traccia", [t] { session::deleteTrack (*t); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    SessionView& view;
    te::AudioTrack::Ptr track;
    juce::OwnedArray<SlotComponent> slots;
    IconButton stopButton { "Ferma le clip della traccia", icons::stop() };
    juce::Slider volume, pan;
    FlatButton muteButton { "M", 11.0f }, soloButton { "S", 11.0f };
    recordingui::ArmButton armButton;
    juce::Label dbLabel;
    LevelMeter meter;
};

//==============================================================================
class SessionView::SceneColumn final : public juce::Component,
                                      private te::TransportControl::Listener
{
public:
    explicit SceneColumn (SessionView& v) : view (v)
    {
        addSceneButton.setBaseColour (colours::panel);
        addSceneButton.setTooltip ("Aggiungi una scena");
        addSceneButton.onClick = [this]
        {
            if (view.edit != nullptr)
                session::addScene (*view.edit);
        };
        addAndMakeVisible (addSceneButton);

        stopAllButton.setIconColour (colours::textDim);
        stopAllButton.setDrawsBackground (true);
        stopAllButton.onClick = [this]
        {
            if (view.edit != nullptr)
                session::stopAllClips (*view.edit);
        };
        addAndMakeVisible (stopAllButton);

        masterVolume.setSliderStyle (juce::Slider::LinearVertical);
        masterVolume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        masterVolume.setRange (-60.0, 6.0, 0.1);
        masterVolume.setSkewFactorFromMidPoint (-12.0);
        masterVolume.setDoubleClickReturnValue (true, 0.0);
        masterVolume.onDragStart = [this]
        {
            if (view.edit != nullptr)
                view.edit->getUndoManager().beginNewTransaction ("Volume master");
        };
        masterVolume.onValueChange = [this]
        {
            if (view.edit != nullptr)
                if (auto vp = view.edit->getMasterVolumePlugin())
                    vp->setVolumeDb ((float) masterVolume.getValue());

            updateLabel();
        };
        addAndMakeVisible (masterVolume);

        dbLabel.setFont (fonts::mono (11.0f));
        dbLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (dbLabel);
        addAndMakeVisible (meter);
    }

    ~SceneColumn() override
    {
        listenTo (nullptr);
    }

    void refresh()
    {
        // Il misuratore del master vive nel contesto di riproduzione, che il motore
        // libera e ricrea (render del freeze, export): ci facciamo avvisare
        listenTo (view.edit);

        // Colori impostati a mano: si riprendono qui, cosi' seguono il tema
        dbLabel.setColour (juce::Label::textColourId, colours::textMid);
        masterVolume.setColour (juce::Slider::trackColourId, colours::hover.brighter (0.15f));

        if (view.edit == nullptr)
            return;

        if (auto vp = view.edit->getMasterVolumePlugin(); vp != nullptr && ! masterVolume.isMouseButtonDown())
            masterVolume.setValue (vp->getVolumeDb(), juce::dontSendNotification);

        // Il contesto di riproduzione puo' essere ricreato: ricollega il misuratore.
        auto context = view.edit->getTransport().getCurrentPlaybackContext();
        meter.setMeasurer (context != nullptr ? &context->masterLevels : nullptr);
        updateLabel();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (12.0f, fonts::Weight::medium));
        g.drawText ("Scene", getLocalBounds().removeFromTop (headerHeight).reduced (8, 0), juce::Justification::centredLeft);

        if (view.edit != nullptr)
        {
            auto scenes = view.edit->getSceneList().getScenes();

            for (int i = 0; i < scenes.size(); ++i)
            {
                auto r = sceneArea (i).toFloat();
                const bool over = hoverScene == i;
                g.setColour (over ? colours::hover : colours::panel);
                g.fillRoundedRectangle (r, metrics::radius);

                icons::draw (g, icons::play(), r.removeFromLeft ((float) launchZone).withSizeKeepingCentre (9.0f, 10.0f),
                             over ? colours::text : colours::textDim);
                g.setColour (colours::text);
                g.setFont (fonts::ui (12.0f));
                g.drawText (sceneName (*scenes[i], i), r.reduced (2.0f, 0.0f), juce::Justification::centredLeft, true);
            }
        }

        g.setColour (colours::panel);
        g.fillRoundedRectangle (getLocalBounds().removeFromBottom (mixerHeight).toFloat(), 8.0f);
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (11.0f, fonts::Weight::medium));
        g.drawText ("Master", getLocalBounds().removeFromBottom (mixerHeight).reduced (10).removeFromBottom (22),
                    juce::Justification::centred);
    }

    void resized() override
    {
        const int numScenes = view.edit != nullptr ? view.edit->getSceneList().getNumScenes() : 0;
        auto below = sceneArea (numScenes);
        addSceneButton.setBounds (below);
        stopAllButton.setBounds (getLocalBounds().withTop (below.getBottom() + metrics::gap)
                                                 .removeFromTop (stopRowHeight).withSizeKeepingCentre (32, stopRowHeight));

        auto mixer = getLocalBounds().removeFromBottom (mixerHeight).reduced (10);
        mixer.removeFromBottom (28);
        dbLabel.setBounds (mixer.removeFromBottom (16));
        mixer.removeFromBottom (4);
        mixer.removeFromTop (40);
        auto faders = mixer.withSizeKeepingCentre (56, mixer.getHeight());
        meter.setBounds (faders.removeFromLeft (14));
        faders.removeFromLeft (12);
        masterVolume.setBounds (faders.removeFromLeft (24));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int s = sceneAt (e.getPosition());

        if (s != hoverScene)
        {
            hoverScene = s;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverScene = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int index = sceneAt (e.getPosition());

        if (view.edit == nullptr || index < 0)
            return;

        if (e.mods.isPopupMenu())
            showMenu (index);
        else
            session::launchScene (*view.edit, index);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (const int index = sceneAt (e.getPosition()); index >= 0 && e.x > launchZone)
            rename (index);
    }

private:
    juce::Rectangle<int> sceneArea (int index) const
    {
        return { 0, headerHeight + metrics::gap + index * (performance::slotHeight() + metrics::gap), getWidth(), performance::slotHeight() };
    }

    int sceneAt (juce::Point<int> p) const
    {
        if (view.edit == nullptr)
            return -1;

        for (int i = 0; i < view.edit->getSceneList().getNumScenes(); ++i)
            if (sceneArea (i).contains (p))
                return i;

        return -1;
    }

    void rename (int index)
    {
        if (auto scene = view.edit->getSceneList().getScenes()[index])
            showRenameBox (*this, sceneArea (index).reduced (2), sceneName (*scene, index), [scene] (const juce::String& n)
            {
                scene->edit.getUndoManager().beginNewTransaction ("Rinomina scena");
                scene->name = n;
            });
    }

    void showMenu (int index)
    {
        auto& list = view.edit->getSceneList();
        juce::PopupMenu m;
        juce::Component::SafePointer<SceneColumn> safe (this);

        m.addItem ("Lancia", [&list, index] { session::launchScene (list.edit, index); });
        m.addItem ("Rinomina", [safe, index] { if (safe != nullptr) safe->rename (index); });
        m.addItem ("Inserisci scena sopra", [&list, index]
        {
            list.edit.getUndoManager().beginNewTransaction ("Inserisci scena");
            list.insertScene (index);
        });
        m.addSeparator();
        m.addItem ("Elimina scena", list.getNumScenes() > 1, false, [&list, index]
        {
            if (auto s = list.getScenes()[index])
            {
                list.edit.getUndoManager().beginNewTransaction ("Elimina scena");
                list.deleteScene (*s);
            }
        });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    void listenTo (te::Edit* e)
    {
        if (e == listenedEdit)
            return;

        // L'edit precedente e' ancora vivo: chi cambia progetto passa prima da nullptr
        meter.setMeasurer (nullptr);

        if (listenedEdit != nullptr)
            listenedEdit->getTransport().removeListener (this);

        listenedEdit = e;

        if (listenedEdit != nullptr)
            listenedEdit->getTransport().addListener (this);
    }

    void playbackContextChanged() override
    {
        // Il contesto vecchio (se c'era) e' gia' distrutto
        meter.forgetMeasurer();
    }

    void updateLabel()
    {
        const auto db = masterVolume.getValue();
        dbLabel.setText (db <= -59.95 ? "-inf" : juce::String (db, 1) + " dB", juce::dontSendNotification);
    }

    SessionView& view;
    int hoverScene = -1;
    FlatButton addSceneButton { "+ Scena", 12.0f };
    IconButton stopAllButton { "Ferma tutte le clip", icons::stop() };
    juce::Slider masterVolume;
    juce::Label dbLabel;
    LevelMeter meter;
    te::Edit* listenedEdit = nullptr;
};

//==============================================================================
class SessionView::Content final : public juce::Component
{
public:
    explicit Content (SessionView& v) : view (v)
    {
        addTrackButton.setBaseColour (colours::panel);
        addTrackButton.setTooltip ("Aggiungi una traccia (Ctrl+T)");
        addTrackButton.onClick = [this]
        {
            if (view.edit != nullptr)
                if (auto t = session::addTrack (*view.edit))
                    view.selectTrack (t);
        };
        addAndMakeVisible (addTrackButton);
        addAndMakeVisible (scenes);
    }

    void rebuild()
    {
        columns.clear();

        if (view.edit != nullptr)
            for (auto t : te::getAudioTracks (*view.edit))
                addAndMakeVisible (columns.add (new TrackColumn (view, *t)));

        scenes.refresh();
        updateSize();
    }

    void updateSize()
    {
        const int numScenes = view.edit != nullptr ? view.edit->getSceneList().getNumScenes() : 0;
        const int w = padding * 2 + columns.size() * (performance::columnWidth() + columnGap) + 44 + columnGap + sceneColumnWidth;
        const int h = juce::jmax (view.viewport.getMaximumVisibleHeight(),
                                  padding * 2 + TrackColumn::requiredHeight (numScenes + 1));
        setSize (juce::jmax (w, view.viewport.getMaximumVisibleWidth()), h);
        resized();
    }

    void refresh()
    {
        for (auto* c : columns)
        {
            c->refresh();
            c->repaintSlots();
        }

        scenes.refresh();
        scenes.repaint();
    }

    void repaintTrack (te::AudioTrack* t)
    {
        for (auto* c : columns)
            if (&c->getTrack() == t || t == nullptr)
                c->repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (padding);

        for (auto* c : columns)
        {
            c->setBounds (r.removeFromLeft (performance::columnWidth()));
            r.removeFromLeft (columnGap);
        }

        addTrackButton.setBounds (r.removeFromLeft (44).removeFromTop (headerHeight));
        r.removeFromLeft (columnGap);
        scenes.setBounds (r.removeFromRight (sceneColumnWidth));
    }

private:
    SessionView& view;
    juce::OwnedArray<TrackColumn> columns;
    SceneColumn scenes { view };
    FlatButton addTrackButton { "+", 18.0f };
};

//==============================================================================
SessionView::SessionView()
{
    content = std::make_unique<Content> (*this);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, true);
    addAndMakeVisible (viewport);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

SessionView::~SessionView()
{
    stopTimer();
    content = nullptr;
}

void SessionView::setEdit (te::Edit* e)
{
    edit = e;
    selectedTrackID = {};
    selectedScene = 0;
    rebuildIfNeeded (true);

    if (edit != nullptr)
        selectTrack (te::getAudioTracks (*edit).getFirst());
}

void SessionView::setRecorder (recording::Recorder* r)
{
    recorder = r;
    content->refresh();
}

te::AudioTrack* SessionView::getSelectedTrack() const
{
    if (edit == nullptr)
        return nullptr;

    for (auto t : te::getAudioTracks (*edit))
        if (t->itemID == selectedTrackID)
            return t;

    return nullptr;
}

void SessionView::selectTrack (te::AudioTrack* t)
{
    const auto id = t != nullptr ? t->itemID : te::EditItemID();

    if (id == selectedTrackID && t != nullptr)
        return;

    selectedTrackID = id;
    content->repaintTrack (nullptr);

    if (onTrackSelected)
        onTrackSelected (t);
}

void SessionView::selectSlot (te::AudioTrack* t, int sceneIndex)
{
    selectedScene = sceneIndex;
    selectTrack (t);
    content->refresh();
    grabKeyboardFocus();

    if (onClipSelected)
        if (auto slot = getSelectedSlot())
            onClipSelected (slot->getClip());
}

te::ClipSlot* SessionView::getSelectedSlot() const
{
    if (auto t = getSelectedTrack())
        return session::getSlot (*t, selectedScene);

    return nullptr;
}

void SessionView::status (const juce::String& s)
{
    if (onStatus)
        onStatus (s);
}

void SessionView::rebuildIfNeeded (bool force)
{
    juce::String sig;

    if (edit != nullptr)
    {
        sig << edit->getSceneList().getNumScenes() << ":";

        for (auto t : te::getAudioTracks (*edit))
            sig << t->itemID.toString() << "/" << t->getClipSlotList().getClipSlots().size() << ",";
    }

    if (force || sig != gridSignature)
    {
        gridSignature = sig;
        content->rebuild();

        if (edit != nullptr && getSelectedTrack() == nullptr)
            selectTrack (te::getAudioTracks (*edit).getFirst());
    }
}

void SessionView::timerCallback()
{
    rebuildIfNeeded();
    content->refresh();
}

void SessionView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
}

void SessionView::resized()
{
    viewport.setBounds (getLocalBounds());
    content->updateSize();
}

bool SessionView::keyPressed (const juce::KeyPress& key)
{
    if (edit == nullptr)
        return false;

    auto tracks = te::getAudioTracks (*edit);
    const int trackIndex = tracks.indexOf (getSelectedTrack());
    const int numScenes = edit->getSceneList().getNumScenes();

    if (key == juce::KeyPress::returnKey)
    {
        if (auto slot = getSelectedSlot())
        {
            if (auto clip = slot->getClip())
                session::launchClip (*clip);
            else
                session::launchScene (*edit, selectedScene);
        }

        return true;
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        if (auto slot = getSelectedSlot(); slot != nullptr && slot->getClip() != nullptr)
        {
            edit->getUndoManager().beginNewTransaction ("Elimina clip");
            session::deleteClip (*slot);
        }

        return true;
    }

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        selectSlot (getSelectedTrack(), juce::jlimit (0, juce::jmax (0, numScenes - 1),
                                                      selectedScene + (key == juce::KeyPress::upKey ? -1 : 1)));
        return true;
    }

    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        const int next = juce::jlimit (0, juce::jmax (0, tracks.size() - 1),
                                       trackIndex + (key == juce::KeyPress::leftKey ? -1 : 1));
        selectSlot (tracks[next], selectedScene);
        return true;
    }

    return false;
}

} // namespace sonora
