/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "ArrangementView.h"
#include "Animation.h"
#include "RecordingUI.h"
#include "session/Arrangement.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace sonora
{

namespace
{
    constexpr int toolbarHeight = 40;
    constexpr int rulerHeight = 24;
    constexpr int headerWidth = 170;
    constexpr int laneGap = 4;
    constexpr int resizeEdge = 6;
    constexpr int padding = 14;

    juce::Colour colourOf (te::Clip& c)
    {
        auto col = c.getColour();

        if (! c.state.hasProperty (te::IDs::colour))
            if (auto t = c.getTrack())
                col = t->getColour();

        return col.isTransparent() ? colours::accent : col;
    }
}

ArrangementView::ArrangementView()
{
    backButton.setBaseColour (colours::panel);
    backButton.setOnColours (colours::warning, colours::background);
    backButton.setTooltip ("Ferma le clip della session e torna a suonare la linea del tempo");
    backButton.onClick = [this]
    {
        if (edit != nullptr)
            arrangement::backToArrangement (*edit);
    };

    snapButton.setBaseColour (colours::panel);
    snapButton.onClick = [this] { showSnapMenu(); };

    fitButton.setBaseColour (colours::panel);
    fitButton.setTooltip ("Mostra tutto il brano");
    fitButton.onClick = [this] { fitToContent(); };

    for (auto* b : { &backButton, &snapButton, &fitButton })
        addAndMakeVisible (b);

    playhead = std::make_unique<anim::PlayheadOverlay> (
        [this]() -> std::optional<int>
        {
            if (edit == nullptr)
                return std::nullopt;

            return xForBeat (edit->tempoSequence.toBeats (edit->getTransport().getPosition()).inBeats());
        },
        [this] { return edit != nullptr && edit->getTransport().isPlaying(); });
    addAndMakeVisible (*playhead);

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

ArrangementView::~ArrangementView()
{
    stopTimer();
}

void ArrangementView::setEdit (te::Edit* e)
{
    edit = e;
    selectedClip = nullptr;
    thumbnails.clear();
    viewStart = 0.0;
    scrollY = 0;

    if (edit != nullptr)
        fitToContent();

    repaint();
}

void ArrangementView::fitToContent()
{
    if (edit == nullptr)
        return;

    if (lanesArea().getWidth() < 200)
    {
        needsFit = true;
        return;
    }

    needsFit = false;
    const auto end = arrangement::getEndOfContent (*edit, 16).inBeats();
    pixelsPerBeat = juce::jlimit (2.0, 400.0, (double) juce::jmax (100, lanesArea().getWidth() - 20) / end);
    viewStart = 0.0;
    repaint();
}

//==============================================================================
juce::Rectangle<int> ArrangementView::rulerArea() const
{
    return getLocalBounds().reduced (padding, 0).withTrimmedTop (toolbarHeight).removeFromTop (rulerHeight)
                           .withTrimmedLeft (headerWidth + 6);
}

juce::Rectangle<int> ArrangementView::lanesArea() const
{
    return getLocalBounds().reduced (padding, 0).withTrimmedTop (toolbarHeight + rulerHeight + 6).withTrimmedBottom (padding)
                           .withTrimmedLeft (headerWidth + 6);
}

int ArrangementView::xForBeat (double beat) const      { return lanesArea().getX() + (int) std::round ((beat - viewStart) * pixelsPerBeat); }
double ArrangementView::beatForX (int x) const         { return viewStart + (x - lanesArea().getX()) / pixelsPerBeat; }
int ArrangementView::laneTop (int index) const         { return lanesArea().getY() - scrollY + index * (laneHeight + laneGap); }

te::AudioTrack* ArrangementView::trackAtY (int y) const
{
    if (edit == nullptr)
        return nullptr;

    auto tracks = te::getAudioTracks (*edit);

    for (int i = 0; i < tracks.size(); ++i)
        if (y >= laneTop (i) && y < laneTop (i) + laneHeight)
            return tracks[i];

    return nullptr;
}

juce::Rectangle<int> ArrangementView::clipRect (te::Clip& c) const
{
    auto tracks = te::getAudioTracks (*edit);
    const int index = tracks.indexOf (dynamic_cast<te::AudioTrack*> (c.getTrack()));

    if (index < 0)
        return {};

    const int x1 = xForBeat (c.getStartBeat().inBeats());
    const int x2 = xForBeat (c.getEndBeat().inBeats());
    return { x1, laneTop (index) + 1, juce::jmax (4, x2 - x1), laneHeight - 2 };
}

te::Clip* ArrangementView::clipAt (juce::Point<int> p) const
{
    if (edit == nullptr || ! lanesArea().contains (p))
        return nullptr;

    if (auto t = trackAtY (p.y))
    {
        auto clips = arrangement::getClips (*t);

        for (int i = clips.size(); --i >= 0;)
            if (clipRect (*clips[i]).contains (p))
                return clips[i];
    }

    return nullptr;
}

te::BeatPosition ArrangementView::snap (double beat) const
{
    return te::BeatPosition::fromBeats (juce::jmax (0.0, std::round (beat / snapBeats) * snapBeats));
}

te::SmartThumbnail* ArrangementView::getThumbnail (te::WaveAudioClip& c)
{
    auto& thumb = thumbnails[c.itemID];

    if (thumb == nullptr)
        thumb = std::make_unique<te::SmartThumbnail> (c.edit.engine, c.getAudioFile(), *this, &c.edit);

    return thumb.get();
}

//==============================================================================
void ArrangementView::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    if (edit == nullptr)
        return;

    auto& ts = edit->tempoSequence;
    const int beatsPerBar = (int) arrangement::barLength (*edit).inBeats();
    const auto lanes = lanesArea();
    const auto ruler = rulerArea();
    auto tracks = te::getAudioTracks (*edit);

    // Righello con le battute e il loop
    g.setColour (colours::panel);
    g.fillRoundedRectangle (ruler.toFloat(), metrics::radius);

    auto& transport = edit->getTransport();
    const auto loop = ts.toBeats (transport.getLoopRange());

    if (loop.getLength() > 0_bd)
    {
        const int lx1 = xForBeat (loop.getStart().inBeats()), lx2 = xForBeat (loop.getEnd().inBeats());
        auto loopRect = juce::Rectangle<int>::leftTopRightBottom (juce::jmax (lx1, ruler.getX()), ruler.getY(),
                                                                  juce::jmin (lx2, ruler.getRight()), ruler.getBottom());
        g.setColour ((transport.looping.get() ? colours::accent : colours::textDim).withAlpha (0.35f));
        g.fillRect (loopRect);
    }

    // Quante battute saltare tra le etichette per non affollarle
    int labelEvery = 1;

    while (pixelsPerBeat * beatsPerBar * labelEvery < 36.0)
        labelEvery *= 2;

    const int firstBar = juce::jmax (0, (int) std::floor (viewStart / beatsPerBar));

    for (int bar = firstBar; ; ++bar)
    {
        const int x = xForBeat ((double) bar * beatsPerBar);

        if (x > lanes.getRight())
            break;

        const bool labelled = bar % labelEvery == 0;

        if (labelled)
        {
            g.setColour (colours::textDim);
            g.setFont (fonts::mono (10.0f));
            g.drawText (juce::String (bar + 1), x + 4, ruler.getY(), 40, ruler.getHeight(), juce::Justification::centredLeft);
        }

        g.setColour (labelled ? colours::outline : colours::divider);
        g.fillRect (x, lanes.getY(), 1, lanes.getHeight());
    }

    // Corsie e intestazioni
    for (int i = 0; i < tracks.size(); ++i)
    {
        auto t = tracks[i];
        const int y = laneTop (i);

        if (y + laneHeight < lanes.getY() || y > lanes.getBottom())
            continue;

        g.saveState();
        g.reduceClipRegion (getLocalBounds().withTop (lanes.getY()).withBottom (lanes.getBottom()));

        auto header = juce::Rectangle<int> (padding, y, headerWidth, laneHeight);
        const bool selected = getSelectedTrack && getSelectedTrack() == t;
        const auto colour = t->getColour().isTransparent() ? colours::accent : t->getColour();
        g.setColour (selected ? colours::selection : colours::panel);
        g.fillRoundedRectangle (header.toFloat(), metrics::radius);
        g.setColour (colour.withAlpha (selected ? 0.2f : 0.1f));
        g.fillRoundedRectangle (header.toFloat(), metrics::radius);

        // Fascia verticale col colore della traccia
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (header.withWidth (4));
            g.setColour (colour);
            g.fillRoundedRectangle (header.toFloat(), metrics::radius);
        }

        if (selected)
        {
            g.setColour (colour.brighter (0.3f));
            g.drawRoundedRectangle (header.toFloat().reduced (0.5f), metrics::radius, 1.0f);
        }

        auto text = header.reduced (12, 0).toFloat();
        g.setColour (colours::text);
        g.setFont (fonts::ui (12.5f, fonts::Weight::medium));
        g.drawText (t->getName(), text, juce::Justification::centredLeft, true);

        if (recorder != nullptr)
        {
            const bool armed = recorder->isArmed (*t);
            const bool hover = isMouseOver() && armArea (i).contains (getMouseXYRelative());
            recordingui::drawArmButton (g, armArea (i).toFloat(), armed, armed && ! recording::isExplicitlyArmed (*t), hover);

            g.setColour (colours::textDim);
            g.setFont (fonts::ui (10.5f));
            g.drawText (recording::getInputName (*t), header.reduced (10, 7), juce::Justification::bottomLeft, true);
        }

        if (t->playSlotClips.get())
        {
            g.setColour (colours::warning);
            g.setFont (fonts::ui (10.0f));
            g.drawText ("session", header.reduced (10, 6), juce::Justification::bottomRight);
        }

        auto lane = juce::Rectangle<int> (lanes.getX(), y, lanes.getWidth(), laneHeight);
        g.setColour (colours::emptySlot.withAlpha (0.6f));
        g.fillRoundedRectangle (lane.toFloat(), metrics::radius);
        g.setColour (colour.withAlpha (selected ? 0.07f : 0.035f));
        g.fillRoundedRectangle (lane.toFloat(), metrics::radius);

        g.reduceClipRegion (lanes);

        for (auto c : arrangement::getClips (*t))
        {
            auto r = clipRect (*c);

            if (r.getRight() >= lanes.getX() && r.getX() <= lanes.getRight())
                drawClip (g, *c, r);
        }

        // Registrazione in corso: dall'inizio alla testina
        if (recorder != nullptr && transport.isPlaying())
        {
            if (auto punchIn = recorder->getPunchInTime (*t))
            {
                const int x1 = xForBeat (ts.toBeats (*punchIn).inBeats());
                const int x2 = xForBeat (ts.toBeats (transport.getPosition()).inBeats());

                if (x2 > x1)
                {
                    auto rec = juce::Rectangle<int> (x1, y, x2 - x1, laneHeight).toFloat();
                    g.setColour (colours::record.withAlpha (0.25f));
                    g.fillRoundedRectangle (rec, metrics::radius);
                    g.setColour (colours::record);
                    g.fillRoundedRectangle (rec.removeFromTop (16.0f), metrics::radius);
                    g.setColour (colours::background);
                    g.setFont (fonts::ui (10.5f, fonts::Weight::medium));
                    g.drawText ("Registrazione", rec.withHeight (16.0f).reduced (6.0f, 0.0f).withY (rec.getY() - 16.0f),
                                juce::Justification::centredLeft, true);
                }
            }
        }

        g.restoreState();
    }

    if (tracks.isEmpty())
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (13.0f));
        g.drawText ("Aggiungi una traccia con Ctrl+T", lanes, juce::Justification::centred);
    }
}

void ArrangementView::drawClip (juce::Graphics& g, te::Clip& c, juce::Rectangle<int> r)
{
    const auto colour = colourOf (c);
    const bool selected = selectedClip.get() == &c;

    g.setColour (colour.withAlpha (0.28f));
    g.fillRoundedRectangle (r.toFloat(), 5.0f);
    auto title = r.removeFromTop (16);
    g.setColour (colour);
    g.fillRoundedRectangle (title.toFloat(), 5.0f);
    g.fillRect (title.withTrimmedTop (8));

    g.setColour (colours::background);
    g.setFont (fonts::ui (11.0f, fonts::Weight::medium));
    g.drawText (c.getName(), title.reduced (6, 0), juce::Justification::centredLeft, true);

    auto body = r.reduced (2, 3);

    if (auto mc = dynamic_cast<te::MidiClip*> (&c))
    {
        // Anteprima delle note
        auto& notes = mc->getSequence().getNotes();
        int lo = 127, hi = 0;

        for (auto n : notes)
        {
            lo = juce::jmin (lo, n->getNoteNumber());
            hi = juce::jmax (hi, n->getNoteNumber());
        }

        if (lo <= hi)
        {
            const int range = juce::jmax (12, hi - lo + 1);
            const double length = juce::jmax (0.001, (c.getEndBeat() - c.getStartBeat()).inBeats());
            const double offset = c.getOffsetInBeats().inBeats();
            g.setColour (colour.brighter (0.4f));

            for (auto n : notes)
            {
                const double s = (n->getStartBeat().inBeats() - offset) / length;
                const double e = (n->getEndBeat().inBeats() - offset) / length;

                if (e <= 0.0 || s >= 1.0)
                    continue;

                const float x1 = (float) body.getX() + (float) body.getWidth() * (float) juce::jmax (0.0, s);
                const float x2 = (float) body.getX() + (float) body.getWidth() * (float) juce::jmin (1.0, e);
                const float y = (float) body.getBottom() - (float) body.getHeight() * (float) (n->getNoteNumber() - lo + 1) / (float) range;
                g.fillRect (x1, y, juce::jmax (1.5f, x2 - x1), 2.0f);
            }
        }
    }
    else if (auto wc = dynamic_cast<te::WaveAudioClip*> (&c))
    {
        if (auto thumb = getThumbnail (*wc))
        {
            const auto pos = c.getPosition();
            g.setColour (colour.brighter (0.3f));
            const auto sourceStart = te::TimePosition() + pos.getOffset();
            thumb->drawChannels (g, body, { sourceStart, sourceStart + pos.getLength() }, 1.0f);
        }
    }

    if (selected)
    {
        g.setColour (colours::text);
        g.drawRoundedRectangle (clipRect (c).toFloat().reduced (0.5f), 5.0f, 1.5f);
    }
}

void ArrangementView::resized()
{
    auto bar = getLocalBounds().reduced (padding, 0).removeFromTop (toolbarHeight).reduced (0, 6);
    bar.removeFromLeft (headerWidth + 6);
    snapButton.setBounds (bar.removeFromLeft (110));
    bar.removeFromLeft (6);
    fitButton.setBounds (bar.removeFromLeft (70));
    backButton.setBounds (bar.removeFromRight (180));

    const auto lanes = lanesArea();
    playhead->setArea (juce::Rectangle<int>::leftTopRightBottom (lanes.getX(), rulerArea().getY(), lanes.getRight(), lanes.getBottom()));

    if (needsFit)
        fitToContent();
}

void ArrangementView::timerCallback()
{
    if (edit == nullptr)
        return;

    // La clip selezionata puo' essere stata eliminata (anche con un undo).
    if (selectedClip != nullptr && selectedClip->getTrack() == nullptr)
        selectClip (nullptr);

    const bool session = arrangement::isAnyTrackPlayingSession (*edit);
    backButton.setToggleState (session, juce::dontSendNotification);
    backButton.setEnabled (session);

    // Segue la testina durante la riproduzione.
    auto& transport = edit->getTransport();

    if (transport.isPlaying() && drag == Drag::none)
    {
        const auto beat = edit->tempoSequence.toBeats (transport.getPosition()).inBeats();
        const double visible = lanesArea().getWidth() / pixelsPerBeat;

        if (beat > viewStart + visible * 0.9 || beat < viewStart)
            viewStart = juce::jmax (0.0, beat - visible * 0.1);
    }

    repaint();
}

//==============================================================================
void ArrangementView::selectClip (te::Clip* c)
{
    selectedClip = c;

    if (c != nullptr && onTrackSelected)
        if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
            onTrackSelected (t);

    if (onClipSelected)
        onClipSelected (c);

    repaint();
}

juce::Rectangle<int> ArrangementView::armArea (int laneIndex) const
{
    return { padding + headerWidth - 30, laneTop (laneIndex) + 8, 22, 22 };
}

void ArrangementView::showHeaderMenu (te::AudioTrack& track)
{
    if (recorder == nullptr)
        return;

    juce::PopupMenu m;
    te::AudioTrack::Ptr t (&track);
    juce::WeakReference<recording::Recorder> rec (recorder);

    m.addSubMenu ("Ingresso", recordingui::inputMenu (*recorder, track));
    m.addItem ("Arma per la registrazione", true, recording::isExplicitlyArmed (track), [t, rec]
    {
        if (rec != nullptr)
            rec->setArmed (*t, ! recording::isExplicitlyArmed (*t));
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    if (edit == nullptr)
        return;

    grabKeyboardFocus();
    drag = Drag::none;
    const auto pos = e.getPosition();
    auto& ts = edit->tempoSequence;

    if (rulerArea().contains (pos))
    {
        dragStartBeat = beatForX (pos.x);

        if (e.mods.isShiftDown())
        {
            drag = Drag::loop;
        }
        else
        {
            drag = Drag::ruler;
            edit->getTransport().setPosition (ts.toTime (snap (dragStartBeat)));
        }

        return;
    }

    // Intestazioni: selezionano la traccia, il cerchio la arma
    if (pos.x < lanesArea().getX())
    {
        if (auto t = trackAtY (pos.y))
        {
            const int index = te::getAudioTracks (*edit).indexOf (t);

            if (recorder != nullptr && armArea (index).expanded (4).contains (pos) && ! e.mods.isPopupMenu())
            {
                recorder->setArmed (*t, ! recording::isExplicitlyArmed (*t));
            }
            else
            {
                if (onTrackSelected)
                    onTrackSelected (t);

                if (e.mods.isPopupMenu())
                    showHeaderMenu (*t);
            }
        }

        repaint();
        return;
    }

    auto c = clipAt (pos);
    selectClip (c);

    if (c == nullptr)
        return;

    if (e.mods.isPopupMenu())
    {
        showClipMenu (*c);
        return;
    }

    edit->getUndoManager().beginNewTransaction ("Modifica clip");
    dragClipStart = c->getStartBeat();
    dragClipLength = c->getEndBeat() - c->getStartBeat();
    dragStartBeat = beatForX (pos.x);
    drag = clipRect (*c).getRight() - pos.x <= resizeEdge ? Drag::resize : Drag::move;
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (edit == nullptr)
        return;

    auto& ts = edit->tempoSequence;
    const double beat = beatForX (e.x);

    if (drag == Drag::ruler)
    {
        edit->getTransport().setPosition (ts.toTime (snap (beat)));
        return;
    }

    if (drag == Drag::loop)
    {
        auto a = snap (juce::jmin (beat, dragStartBeat)), b = snap (juce::jmax (beat, dragStartBeat));

        if (b > a)
        {
            edit->getTransport().setLoopRange (ts.toTime (te::BeatRange (a, b)));
            edit->getTransport().looping = true;
        }

        return;
    }

    if (selectedClip == nullptr)
        return;

    if (drag == Drag::move)
    {
        const auto newStart = snap (dragClipStart.inBeats() + beat - dragStartBeat);
        auto target = trackAtY (e.y);
        arrangement::moveClip (*selectedClip, newStart, target);
    }
    else if (drag == Drag::resize)
    {
        const auto end = snap (dragClipStart.inBeats() + dragClipLength.inBeats() + beat - dragStartBeat);
        arrangement::resizeClip (*selectedClip, end - dragClipStart, te::BeatDuration::fromBeats (snapBeats));
    }

    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (edit == nullptr || ! lanesArea().contains (e.getPosition()) || clipAt (e.getPosition()) != nullptr)
        return;

    if (auto t = trackAtY (e.y))
    {
        edit->getUndoManager().beginNewTransaction ("Nuova clip MIDI");
        // Come nella session view, una clip nuova parte dall'inizio della battuta
        const double bar = arrangement::barLength (*edit).inBeats();
        const auto start = te::BeatPosition::fromBeats (std::floor (beatForX (e.x) / bar) * bar);

        if (auto c = arrangement::createMidiClip (*t, start, 1))
            selectClip (c);
    }
}

void ArrangementView::mouseMove (const juce::MouseEvent& e)
{
    auto newCursor = juce::MouseCursor::NormalCursor;

    if (auto c = clipAt (e.getPosition()))
        newCursor = clipRect (*c).getRight() - e.x <= resizeEdge ? juce::MouseCursor::LeftRightResizeCursor
                                                              : juce::MouseCursor::DraggingHandCursor;

    setMouseCursor (newCursor);

    if (e.x < lanesArea().getX())
        repaint (0, 0, lanesArea().getX(), getHeight());
}

void ArrangementView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown())
    {
        const double anchor = beatForX (e.x);
        pixelsPerBeat = juce::jlimit (2.0, 400.0, pixelsPerBeat * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
        viewStart = juce::jmax (0.0, anchor - (e.x - lanesArea().getX()) / pixelsPerBeat);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
        viewStart = juce::jmax (0.0, viewStart - d * 60.0 / pixelsPerBeat);
    }
    else if (edit != nullptr)
    {
        const int total = te::getAudioTracks (*edit).size() * (laneHeight + laneGap);
        scrollY = juce::jlimit (0, juce::jmax (0, total - lanesArea().getHeight()), scrollY - (int) (w.deltaY * 120.0f));
    }

    repaint();
}

bool ArrangementView::keyPressed (const juce::KeyPress& key)
{
    if (edit == nullptr || selectedClip == nullptr)
        return false;

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        edit->getUndoManager().beginNewTransaction ("Elimina clip");
        arrangement::deleteClip (*selectedClip);
        selectClip (nullptr);
        return true;
    }

    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'd' || key.getKeyCode() == 'D'))
    {
        edit->getUndoManager().beginNewTransaction ("Duplica clip");

        if (auto copy = arrangement::duplicateClip (*selectedClip))
            selectClip (copy);

        return true;
    }

    return false;
}

void ArrangementView::showClipMenu (te::Clip& c)
{
    te::Clip::Ptr clip (&c);
    juce::PopupMenu m;
    juce::Component::SafePointer<ArrangementView> safe (this);

    m.addItem ("Duplica", [safe, clip]
    {
        clip->edit.getUndoManager().beginNewTransaction ("Duplica clip");

        if (auto copy = arrangement::duplicateClip (*clip); copy != nullptr && safe != nullptr)
            safe->selectClip (copy);
    });
    m.addSeparator();
    m.addItem ("Elimina", [safe, clip]
    {
        clip->edit.getUndoManager().beginNewTransaction ("Elimina clip");
        arrangement::deleteClip (*clip);

        if (safe != nullptr)
            safe->selectClip (nullptr);
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ArrangementView::showSnapMenu()
{
    juce::PopupMenu m;
    const std::pair<const char*, double> sizes[] = { { "1 battuta", 0.0 }, { "1/2", 2.0 }, { "1/4", 1.0 },
                                                     { "1/8", 0.5 }, { "1/16", 0.25 } };

    for (auto [name, beats] : sizes)
    {
        const double value = beats > 0.0 ? beats : (edit != nullptr ? arrangement::barLength (*edit).inBeats() : 4.0);
        m.addItem (name, true, std::abs (snapBeats - value) < 1.0e-6, [this, name = juce::String (name), value]
        {
            snapBeats = value;
            snapButton.setButtonText ("Griglia: " + name);
        });
    }

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&snapButton));
}

//==============================================================================
bool ArrangementView::isInterestedInFileDrag (const juce::StringArray& files)
{
    juce::File f (files[0]);
    return f.hasFileExtension (te::soundFileExtensions) || f.hasFileExtension (te::midiFileExtensions);
}

void ArrangementView::filesDropped (const juce::StringArray& files, int x, int y)
{
    dropFile (juce::File (files[0]), { x, y });
}

bool ArrangementView::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("file:");
}

void ArrangementView::itemDropped (const SourceDetails& d)
{
    dropFile (juce::File (d.description.toString().fromFirstOccurrenceOf ("file:", false, false)), d.localPosition);
}

void ArrangementView::dropFile (const juce::File& f, juce::Point<int> pos)
{
    if (edit == nullptr)
        return;

    auto t = trackAtY (pos.y);

    if (t == nullptr)
    {
        if (onStatus)
            onStatus ("Rilascia il file su una traccia");

        return;
    }

    edit->getUndoManager().beginNewTransaction ("Importa " + f.getFileName());
    const auto start = te::BeatPosition::fromBeats (std::floor (juce::jmax (0.0, beatForX (pos.x)) / snapBeats) * snapBeats);

    if (auto c = arrangement::createClipFromFile (*t, start, f))
    {
        selectClip (c);

        if (onStatus)
            onStatus ("Importato " + f.getFileName() + " su " + t->getName());
    }
    else if (onStatus)
    {
        onStatus ("Formato non supportato: " + f.getFileName());
    }
}

} // namespace sonora
