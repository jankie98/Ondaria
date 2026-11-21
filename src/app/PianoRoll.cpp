/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "PianoRoll.h"
#include "Animation.h"
#include "FreezeUI.h"
#include "session/Notes.h"
#include "session/Session.h"

namespace te = tracktion;
using namespace tracktion::literals;

namespace sonora
{

namespace
{
    constexpr int keysWidth = 52;
    constexpr int rulerHeight = 20;
    constexpr int velocityHeight = 46;
    constexpr int toolbarHeight = 30;
    constexpr int resizeEdge = 6;

    bool isBlackKey (int pitch)
    {
        return juce::MidiMessage::isMidiNoteBlack (pitch);
    }
}

//==============================================================================
class PianoRoll::Grid final : public juce::Component
{
public:
    explicit Grid (PianoRoll& o) : owner (o)
    {
        setWantsKeyboardFocus (true);

        playhead = std::make_unique<anim::PlayheadOverlay> (
            [this]() -> std::optional<int>
            {
                if (auto beat = playBeat())
                    return xForBeat (*beat);

                return std::nullopt;
            },
            [this] { return owner.clip != nullptr && owner.clip->edit.getTransport().isPlaying(); });

        addAndMakeVisible (*playhead);
    }

    /** Posizione di riproduzione dentro la clip (in beat), se sta suonando. */
    std::optional<double> playBeat() const
    {
        auto c = owner.clip;

        if (c == nullptr)
            return std::nullopt;

        const double clipLength = notes::getClipLength (*c).inBeats();

        if (c->getClipSlot() != nullptr)
        {
            if (auto progress = session::getPlayProgress (*c))
                return *progress * clipLength;

            return std::nullopt;
        }

        // Clip dell'arrangement: segue la testina del trasporto
        auto& transport = c->edit.getTransport();

        if (! transport.isPlaying())
            return std::nullopt;

        const auto pos = transport.getPosition();

        if (! c->getEditTimeRange().contains (pos))
            return std::nullopt;

        double beat = c->edit.tempoSequence.toBeats (pos).inBeats() - c->getStartBeat().inBeats() + c->getOffsetInBeats().inBeats();

        if (c->isLooping())
        {
            const double loopLength = c->getLoopLengthBeats().inBeats();

            if (loopLength > 0.0)
                beat = c->getLoopStartBeats().inBeats() + std::fmod (beat, loopLength);
        }

        return beat;
    }

    te::BeatDuration gridSize = te::BeatDuration::fromBeats (0.25);
    std::vector<te::MidiNote*> selection;

    void reset()
    {
        selection.clear();
        viewStart = 0.0;
        fitWidth();
        fitNotes();
        repaint();
    }

    void fitWidth()
    {
        if (auto c = owner.clip)
        {
            const double length = juce::jmax (1.0, notes::getClipLength (*c).inBeats());
            pixelsPerBeat = juce::jmax (4.0, (double) noteArea().getWidth() / length);
        }
    }

    /** Centra la vista sulle note della clip (o sul Do centrale se e' vuota). */
    void fitNotes()
    {
        int lo = 127, hi = 0;

        if (auto c = owner.clip)
            for (auto n : c->getSequence().getNotes())
            {
                lo = juce::jmin (lo, n->getNoteNumber());
                hi = juce::jmax (hi, n->getNoteNumber());
            }

        const int visibleRows = juce::jmax (1, noteArea().getHeight() / rowHeight);
        const int centre = lo <= hi ? (lo + hi) / 2 : 60;
        topPitch = juce::jlimit (visibleRows - 1, 127, centre + visibleRows / 2);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::sidebar);
        auto c = owner.clip;

        if (c == nullptr)
            return;

        const auto area = noteArea();
        const double clipLength = notes::getClipLength (*c).inBeats();
        const int beatsPerBar = c->edit.tempoSequence.getTimeSigAt (0_tp).numerator.get();
        const auto colour = clipColourOf (*c);

        // Righe dei tasti
        for (int p = topPitch; p >= 0; --p)
        {
            const int y = yForPitch (p);

            if (y > area.getBottom())
                break;

            g.setColour (isBlackKey (p) ? colours::emptySlot : colours::panel);
            g.fillRect (area.getX(), y, area.getWidth(), rowHeight);

            if (p % 12 == 0)
            {
                g.setColour (colours::outline);
                g.fillRect (area.getX(), y + rowHeight - 1, area.getWidth(), 1);
            }

            // Tastiera a sinistra
            auto key = juce::Rectangle<int> (0, y, keysWidth, rowHeight);
            g.setColour (isBlackKey (p) ? colours::panelLight : colours::textMid.withAlpha (0.9f));
            g.fillRect (key.reduced (0, 0).withTrimmedBottom (1));

            if (p % 12 == 0)
            {
                g.setColour (colours::background);
                g.setFont (fonts::mono (9.5f));
                g.drawText (notes::noteName (p), key.withTrimmedRight (4), juce::Justification::centredRight);
            }
        }

        // Fuori dalla clip
        const int endX = xForBeat (clipLength);

        if (endX < area.getRight())
        {
            g.setColour (colours::background.withAlpha (0.6f));
            g.fillRect (juce::Rectangle<int>::leftTopRightBottom (juce::jmax (endX, area.getX()), area.getY(), area.getRight(), area.getBottom()));
        }

        // Linee verticali: battute, beat e griglia
        const double step = gridSize.inBeats();
        const double firstBeat = std::floor (viewStart / step) * step;

        for (double b = firstBeat; ; b += step)
        {
            const int x = xForBeat (b);

            if (x > area.getRight())
                break;

            if (x < area.getX())
                continue;

            const bool bar = std::fmod (b + 1.0e-6, (double) beatsPerBar) < 1.0e-3;
            const bool beat = std::fmod (b + 1.0e-6, 1.0) < 1.0e-3;

            if (! beat && pixelsPerBeat * step < 6.0)
                continue;

            g.setColour (bar ? colours::hover.brighter (0.2f) : beat ? colours::outline : colours::divider);
            g.fillRect (x, area.getY(), 1, area.getHeight() + velocityHeight + 4);

            if (bar)
            {
                g.setColour (colours::textDim);
                g.setFont (fonts::mono (10.0f));
                g.drawText (juce::String ((int) std::round (b) / beatsPerBar + 1), x + 4, 0, 40, rulerHeight,
                            juce::Justification::centredLeft);
            }
        }

        // Note
        g.saveState();
        g.reduceClipRegion (area);

        for (auto n : c->getSequence().getNotes())
        {
            auto r = rectForNote (*n);

            if (! r.intersects (area))
                continue;

            const bool selected = isSelected (n);
            const float alpha = 0.45f + 0.55f * (float) n->getVelocity() / 127.0f;
            g.setColour (colour.withAlpha (alpha));
            g.fillRoundedRectangle (r.toFloat().reduced (0.5f, 0.5f), 3.0f);

            if (selected)
            {
                g.setColour (colours::text);
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f, 0.5f), 3.0f, 1.5f);
            }
        }

        g.restoreState();

        // Righello e corsia della velocity
        g.setColour (colours::divider);
        g.fillRect (keysWidth, rulerHeight - 1, getWidth() - keysWidth, 1);

        auto vel = velocityArea();
        g.setColour (colours::panel);
        g.fillRect (vel);
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (10.0f));
        g.drawText ("Velocity", juce::Rectangle<int> (0, vel.getY(), keysWidth - 4, vel.getHeight()), juce::Justification::centredRight);

        g.saveState();
        g.reduceClipRegion (vel);

        for (auto n : c->getSequence().getNotes())
        {
            const int x = xForBeat (n->getStartBeat().inBeats());
            const int h = (int) std::round ((vel.getHeight() - 6) * n->getVelocity() / 127.0);
            g.setColour (isSelected (n) ? colours::text : colour);
            g.fillRect (x, vel.getBottom() - 3 - h, 3, h);
            g.fillEllipse ((float) x - 1.5f, (float) (vel.getBottom() - 3 - h) - 3.0f, 6.0f, 6.0f);
        }

        g.restoreState();
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto c = owner.clip;

        if (c == nullptr)
            return;

        grabKeyboardFocus();
        c->edit.getUndoManager().beginNewTransaction ("Modifica note");
        dragMode = DragMode::none;

        const auto pos = e.getPosition();

        if (pos.x < keysWidth && noteArea().contains (pos.withX (keysWidth + 1)))
        {
            previewPitch = pitchForY (pos.y);
            preview (previewPitch, true);
            return;
        }

        if (velocityArea().contains (pos))
        {
            dragMode = DragMode::velocity;
            setVelocityAt (pos);
            return;
        }

        if (! noteArea().contains (pos))
            return;

        const int pitch = pitchForY (pos.y);
        const auto beat = te::BeatPosition::fromBeats (beatForX (pos.x));

        if (auto n = notes::findNote (*c, pitch, beat))
        {
            if (e.mods.isPopupMenu())
            {
                eraseNote (n);
                return;
            }

            if (! isSelected (n))
            {
                if (! e.mods.isShiftDown())
                    selection.clear();

                selection.push_back (n);
            }

            activeNote = n;
            dragStartBeat = n->getStartBeat();
            dragStartPitch = n->getNoteNumber();
            dragStartLength = n->getLengthBeats();
            dragMode = rectForNote (*n).getRight() - pos.x <= resizeEdge ? DragMode::resize : DragMode::move;
            preview (pitch, true);
            previewPitch = pitch;
        }
        else
        {
            if (e.mods.isPopupMenu())
                return;

            const auto start = notes::snapDown (beat, gridSize);
            auto added = notes::addNote (*c, pitch, start, lastLength);

            if (added == nullptr)
                return;

            selection = { added };
            activeNote = added;
            dragStartBeat = added->getStartBeat();
            dragStartPitch = pitch;
            dragStartLength = added->getLengthBeats();
            dragMode = DragMode::create;
            preview (pitch, true);
            previewPitch = pitch;
        }

        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto c = owner.clip;

        if (c == nullptr)
            return;

        const auto pos = e.getPosition();

        if (dragMode == DragMode::velocity)
        {
            setVelocityAt (pos);
            return;
        }

        if (activeNote == nullptr || ! stillExists (activeNote))
            return;

        const double deltaBeats = (double) e.getDistanceFromDragStartX() / pixelsPerBeat;

        if (dragMode == DragMode::move)
        {
            const int deltaPitch = (int) std::round ((double) -e.getDistanceFromDragStartY() / rowHeight);
            auto target = notes::snapNearest (dragStartBeat + te::BeatDuration::fromBeats (deltaBeats), gridSize);
            const auto beatShift = target - dragStartBeat;
            const int pitchShift = dragStartPitch + deltaPitch - activeNote->getNoteNumber();

            // Sposta tutte le selezionate della stessa quantita'.
            const auto activeShift = (dragStartBeat + beatShift) - activeNote->getStartBeat();

            for (auto n : selection)
                if (stillExists (n))
                    notes::moveNote (*c, *n, n->getStartBeat() + activeShift, n->getNoteNumber() + pitchShift);

            if (activeNote->getNoteNumber() != previewPitch)
            {
                preview (previewPitch, false);
                previewPitch = activeNote->getNoteNumber();
                preview (previewPitch, true);
            }
        }
        else if (dragMode == DragMode::create)
        {
            // Trascinando mentre si disegna, la nota arriva fin sotto il mouse:
            // la lunghezza ricordata vale solo per il clic semplice.
            if (std::abs (e.getDistanceFromDragStartX()) < 4)
                return;

            const auto mouseBeat = te::BeatPosition::fromBeats (beatForX (pos.x));
            auto end = notes::snapNearest (mouseBeat, gridSize);
            notes::resizeNote (*c, *activeNote, end - activeNote->getStartBeat(), gridSize);
        }
        else if (dragMode == DragMode::resize)
        {
            auto end = notes::snapNearest (dragStartBeat + dragStartLength + te::BeatDuration::fromBeats (deltaBeats), gridSize);
            notes::resizeNote (*c, *activeNote, end - activeNote->getStartBeat(), gridSize);
        }

        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (previewPitch >= 0)
            preview (previewPitch, false);

        if ((dragMode == DragMode::resize || dragMode == DragMode::create) && activeNote != nullptr && stillExists (activeNote))
            lastLength = activeNote->getLengthBeats();

        previewPitch = -1;
        activeNote = nullptr;
        dragMode = DragMode::none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto c = owner.clip;

        if (c == nullptr || ! noteArea().contains (e.getPosition()))
            return;

        if (auto n = notes::findNote (*c, pitchForY (e.y), te::BeatPosition::fromBeats (beatForX (e.x))))
            eraseNote (n);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto c = owner.clip;
        auto newCursor = juce::MouseCursor::NormalCursor;

        if (c != nullptr && noteArea().contains (e.getPosition()))
            if (auto n = notes::findNote (*c, pitchForY (e.y), te::BeatPosition::fromBeats (beatForX (e.x))))
                newCursor = rectForNote (*n).getRight() - e.x <= resizeEdge ? juce::MouseCursor::LeftRightResizeCursor
                                                                         : juce::MouseCursor::DraggingHandCursor;

        setMouseCursor (newCursor);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown())
        {
            const double anchor = beatForX (e.x);
            pixelsPerBeat = juce::jlimit (4.0, 800.0, pixelsPerBeat * (w.deltaY > 0 ? 1.2 : 1.0 / 1.2));
            viewStart = juce::jmax (0.0, anchor - (e.x - noteArea().getX()) / pixelsPerBeat);
        }
        else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
        {
            const float d = std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
            viewStart = juce::jmax (0.0, viewStart - d * 40.0 / pixelsPerBeat);
        }
        else
        {
            const int visibleRows = noteArea().getHeight() / rowHeight;
            topPitch = juce::jlimit (visibleRows - 1, 127, topPitch + (w.deltaY > 0 ? 3 : -3));
        }

        repaint();
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        auto c = owner.clip;

        if (c == nullptr)
            return false;

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            c->edit.getUndoManager().beginNewTransaction ("Elimina note");

            for (auto n : selection)
                if (stillExists (n))
                    notes::deleteNote (*c, *n);

            selection.clear();
            repaint();
            return true;
        }

        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            const int shift = (key == juce::KeyPress::upKey ? 1 : -1) * (key.getModifiers().isShiftDown() ? 12 : 1);
            c->edit.getUndoManager().beginNewTransaction ("Trasponi note");

            for (auto n : selection)
                if (stillExists (n))
                    notes::moveNote (*c, *n, n->getStartBeat(), n->getNoteNumber() + shift);

            repaint();
            return true;
        }

        if (key.getModifiers().isCommandDown() && (key.getKeyCode() == 'a' || key.getKeyCode() == 'A'))
        {
            selection.clear();

            for (auto n : c->getSequence().getNotes())
                selection.push_back (n);

            repaint();
            return true;
        }

        return false;
    }

    void resized() override
    {
        if (pixelsPerBeat <= 0.0)
            fitWidth();

        playhead->setArea (noteArea().withTop (0));
    }

private:
    enum class DragMode { none, create, move, resize, velocity };

    PianoRoll& owner;
    int topPitch = 72, rowHeight = 12, previewPitch = -1;
    double pixelsPerBeat = 0.0, viewStart = 0.0;
    DragMode dragMode = DragMode::none;
    te::MidiNote* activeNote = nullptr;
    te::BeatPosition dragStartBeat;
    te::BeatDuration dragStartLength, lastLength = te::BeatDuration::fromBeats (0.25);
    int dragStartPitch = 60;
    std::unique_ptr<anim::PlayheadOverlay> playhead;

    static juce::Colour clipColourOf (te::MidiClip& c)
    {
        return c.getColour().isTransparent() ? colours::accent : c.getColour();
    }

    juce::Rectangle<int> noteArea() const
    {
        return getLocalBounds().withTrimmedLeft (keysWidth).withTrimmedTop (rulerHeight).withTrimmedBottom (velocityHeight + 4);
    }

    juce::Rectangle<int> velocityArea() const
    {
        return getLocalBounds().withTrimmedLeft (keysWidth).removeFromBottom (velocityHeight);
    }

    int xForBeat (double beat) const        { return noteArea().getX() + (int) std::round ((beat - viewStart) * pixelsPerBeat); }
    double beatForX (int x) const           { return viewStart + (x - noteArea().getX()) / pixelsPerBeat; }
    int yForPitch (int pitch) const         { return noteArea().getY() + (topPitch - pitch) * rowHeight; }
    int pitchForY (int y) const             { return juce::jlimit (0, 127, topPitch - (y - noteArea().getY()) / rowHeight); }

    juce::Rectangle<int> rectForNote (te::MidiNote& n) const
    {
        const int x1 = xForBeat (n.getStartBeat().inBeats());
        const int x2 = xForBeat (n.getEndBeat().inBeats());
        return { x1, yForPitch (n.getNoteNumber()), juce::jmax (3, x2 - x1), rowHeight };
    }

    bool isSelected (te::MidiNote* n) const
    {
        return std::find (selection.begin(), selection.end(), n) != selection.end();
    }

    bool stillExists (te::MidiNote* n) const
    {
        return owner.clip != nullptr && owner.clip->getSequence().getNotes().contains (n);
    }

    void eraseNote (te::MidiNote* n)
    {
        selection.erase (std::remove (selection.begin(), selection.end(), n), selection.end());
        notes::deleteNote (*owner.clip, *n);
        activeNote = nullptr;
        dragMode = DragMode::none;
        repaint();
    }

    void setVelocityAt (juce::Point<int> pos)
    {
        auto c = owner.clip;
        auto vel = velocityArea();
        const int value = (int) std::round (127.0 * (vel.getBottom() - 3 - pos.y) / (vel.getHeight() - 6));

        // Le note sotto il mouse (o le selezionate, se ce ne sono sotto il mouse).
        const double beat = beatForX (pos.x);

        for (auto n : c->getSequence().getNotes())
        {
            const double start = n->getStartBeat().inBeats();

            if (std::abs (xForBeat (start) - pos.x) <= 4 || (beat >= start && beat < n->getEndBeat().inBeats() && isSelected (n)))
                notes::setVelocity (*c, *n, value);
        }

        repaint();
    }

    void preview (int pitch, bool on)
    {
        if (owner.onPreviewNote && pitch >= 0)
            owner.onPreviewNote (pitch, on);
    }
};

//==============================================================================
PianoRoll::PianoRoll()
{
    grid = std::make_unique<Grid> (*this);
    addAndMakeVisible (*grid);

    for (auto* b : { &gridButton, &lengthButton, &quantiseButton, &foldButton })
    {
        b->setBaseColour (colours::panel);
        addAndMakeVisible (b);
    }

    gridButton.setTooltip ("Griglia");
    lengthButton.setTooltip ("Lunghezza della clip");
    quantiseButton.setTooltip ("Allinea le note alla griglia");
    foldButton.setTooltip ("Mostra l'intervallo di note usato");

    gridButton.onClick = [this] { showGridMenu(); };
    lengthButton.onClick = [this] { showLengthMenu(); };
    quantiseButton.onClick = [this]
    {
        if (clip != nullptr)
        {
            clip->edit.getUndoManager().beginNewTransaction ("Quantizza");
            notes::quantise (*clip, grid->gridSize);
            grid->repaint();
        }
    };
    foldButton.onClick = [this] { grid->fitNotes(); grid->repaint(); };

    startTimerHz (30);
}

PianoRoll::~PianoRoll() = default;

void PianoRoll::setClip (te::MidiClip* c)
{
    if (clip.get() == c)
        return;

    clip = c;
    resized();
    grid->reset();
    updateToolbar();
    repaint();
}

void PianoRoll::timerCallback()
{
    // La clip puo' essere stata eliminata (anche con un undo).
    if (clip != nullptr && clip->getClipSlot() == nullptr && clip->getTrack() == nullptr)
        setClip (nullptr);

    updateToolbar();

    // Clip di una traccia congelata: le note si vedono ma non si modificano
    const bool frozen = isFrozen();

    if (grid->isEnabled() == frozen)
    {
        grid->setEnabled (! frozen);
        grid->setAlpha (frozen ? 0.55f : 1.0f);

        for (auto* b : { &lengthButton, &quantiseButton })
            b->setEnabled (! frozen);

        repaint();
    }
}

bool PianoRoll::isFrozen() const
{
    auto t = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
    return t != nullptr && freeze::isFrozen (*t);
}

void PianoRoll::updateToolbar()
{
    if (clip == nullptr)
        return;

    auto& ts = clip->edit.tempoSequence;
    const int beatsPerBar = ts.getTimeSigAt (0_tp).numerator.get();
    const int bars = juce::roundToInt (notes::getClipLength (*clip).inBeats() / beatsPerBar);
    const auto text = juce::String (bars) + (bars == 1 ? " battuta" : " battute");

    // La lunghezza puo' cambiare anche da fuori (per esempio allungando la clip nell'arrangement)
    if (lengthButton.getButtonText() != text)
    {
        lengthButton.setButtonText (text);
        grid->fitWidth();
        grid->repaint();
    }
}

void PianoRoll::showGridMenu()
{
    juce::PopupMenu m;
    const std::pair<const char*, double> sizes[] = { { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 },
                                                     { "1/8 T", 1.0 / 3.0 }, { "1/16 T", 1.0 / 6.0 } };

    for (auto [name, beats] : sizes)
        m.addItem (name, true, std::abs (grid->gridSize.inBeats() - beats) < 1.0e-6, [this, name = juce::String (name), beats = beats]
        {
            grid->gridSize = te::BeatDuration::fromBeats (beats);
            gridButton.setButtonText (name);
            grid->repaint();
        });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gridButton));
}

void PianoRoll::showLengthMenu()
{
    juce::PopupMenu m;

    for (int bars : { 1, 2, 4, 8, 16 })
        m.addItem (juce::String (bars) + (bars == 1 ? " battuta" : " battute"), [this, bars]
        {
            if (clip != nullptr)
            {
                clip->edit.getUndoManager().beginNewTransaction ("Lunghezza clip");
                notes::setClipLengthInBars (*clip, bars);
                grid->fitWidth();
                updateToolbar();
                grid->repaint();
            }
        });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&lengthButton));
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (colours::sidebar);

    if (clip == nullptr)
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (12.5f));
        g.drawText ("Seleziona una clip MIDI nella session view per modificarne le note", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    auto bar = getLocalBounds().removeFromTop (toolbarHeight);
    g.setColour (colours::textDim);
    g.setFont (fonts::ui (11.5f));
    g.drawText ("Griglia", bar.removeFromLeft (48), juce::Justification::centredLeft);

    if (isFrozen())
    {
        auto note = bar.withTrimmedBottom (4).removeFromRight (300).toFloat();
        freezeui::drawSnowflake (g, note.removeFromLeft (22.0f).withSizeKeepingCentre (12.0f, 12.0f), freezeui::iceColour());
        g.setColour (colours::textMid);
        g.drawText ("Traccia congelata: note in sola lettura", note, juce::Justification::centredLeft, true);
    }
}

void PianoRoll::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (toolbarHeight).withTrimmedBottom (4);
    bar.removeFromLeft (48);
    gridButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (6);
    lengthButton.setBounds (bar.removeFromLeft (92));
    bar.removeFromLeft (6);
    quantiseButton.setBounds (bar.removeFromLeft (86));
    bar.removeFromLeft (6);
    foldButton.setBounds (bar.removeFromLeft (86));

    for (auto* b : { &gridButton, &lengthButton, &quantiseButton, &foldButton })
        b->setVisible (clip != nullptr);

    grid->setVisible (clip != nullptr);
    grid->setBounds (r);
    grid->fitWidth();
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    if (isFrozen())
        return false;

    return grid->keyPressed (k);
}

} // namespace sonora
