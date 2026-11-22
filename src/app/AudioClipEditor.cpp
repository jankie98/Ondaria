/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "AudioClipEditor.h"
#include "Animation.h"
#include "FreezeUI.h"
#include "session/Freeze.h"
#include "session/Warp.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    constexpr int controlsWidth = 236;
    constexpr int rowHeight = 26;
    constexpr int rowGap = 6;
    constexpr int labelWidth = 82;
    constexpr int rulerHeight = 18;
    constexpr int markerGrab = 5;

    const juce::Colour markerColour { 0xfff2c94c };

    /** Un tratto lineare: [s0, s1] del file su [x0, x1]. */
    void drawSpan (juce::Graphics& g, te::SmartThumbnail& thumb, juce::Rectangle<float> area,
                   float x0, float x1, double s0, double s1)
    {
        x0 = juce::jmax (x0, area.getX());
        x1 = juce::jmin (x1, area.getRight());

        if (x1 - x0 < 0.5f || s1 <= s0)
            return;

        thumb.drawChannels (g, juce::Rectangle<float> (x0, area.getY(), x1 - x0, area.getHeight()).toNearestInt(),
                            { te::TimePosition::fromSeconds (s0), te::TimePosition::fromSeconds (s1) }, 1.0f);
    }
}

//==============================================================================
void drawWarpedWaveform (juce::Graphics& g, te::SmartThumbnail& thumb, const te::AudioClipBase& clip,
                         juce::Rectangle<float> area, juce::Colour colour)
{
    const auto pos = clip.getPosition();
    const double start = pos.getStart().inSeconds(), length = pos.getLength().inSeconds();

    if (length <= 0.0 || area.getWidth() < 1.0f)
        return;

    g.setColour (colour);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());

    // Tratti da ~6 px: dentro ognuno la mappa tempo -> file e' quasi lineare
    const int numSpans = juce::jlimit (1, 600, (int) (area.getWidth() / 6.0f));
    const double secondsPerPixel = length / area.getWidth();
    const double sourceLength = warp::sourceLength (clip);

    for (int i = 0; i < numSpans; ++i)
    {
        const float x0 = area.getX() + area.getWidth() * (float) i / (float) numSpans;
        const float x1 = area.getX() + area.getWidth() * (float) (i + 1) / (float) numSpans;
        const auto t0 = te::TimePosition::fromSeconds (start + (x0 - area.getX()) * secondsPerPixel);
        const auto t1 = te::TimePosition::fromSeconds (start + (x1 - area.getX()) * secondsPerPixel - 1.0e-6);
        const auto s0 = warp::sourceSecondsAt (clip, t0), s1 = warp::sourceSecondsAt (clip, t1);

        if (! s0 || ! s1 || *s1 <= *s0 || *s0 >= sourceLength)
            continue;

        drawSpan (g, thumb, area, x0, x1, juce::jmax (0.0, *s0), juce::jmin (sourceLength, *s1));
    }
}

//==============================================================================
class AudioClipEditor::WaveView final : public juce::Component
{
public:
    explicit WaveView (AudioClipEditor& o) : owner (o)
    {
        playhead = std::make_unique<anim::PlayheadOverlay> (
            [this]() -> std::optional<int>
            {
                if (auto beat = playBeat())
                    return juce::roundToInt (xForBeat (*beat));

                return std::nullopt;
            },
            [this] { return owner.clip != nullptr && owner.clip->edit.getTransport().isPlaying(); });

        addAndMakeVisible (*playhead);
    }

    void setClip (te::WaveAudioClip* c)
    {
        thumb.reset();
        dragging = -1;

        if (c != nullptr)
            thumb = std::make_unique<te::SmartThumbnail> (c->edit.engine, te::AudioFile (c->edit.engine, c->getOriginalFile()), *this, &c->edit);

        fit();
    }

    /** Tutto il file in vista. */
    void fit()
    {
        if (auto c = owner.clip)
        {
            const double total = juce::jmax (1.0, fileBeats (*c));
            pixelsPerBeat = juce::jmax (2.0, (getWidth() - 16.0) / total);
            firstBeat = -8.0 / pixelsPerBeat;
        }

        repaint();
    }

    void resized() override
    {
        playhead->setArea (getLocalBounds());

        if (! fitted && getWidth() > 0)
        {
            fitted = true;
            fit();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colours::background);
        auto c = owner.clip;

        if (c == nullptr || thumb == nullptr)
            return;

        const auto bounds = getLocalBounds().toFloat();
        const auto waveArea = bounds.withTrimmedTop ((float) rulerHeight).reduced (0.0f, 4.0f);
        const auto colour = c->getColour();
        const bool warped = warp::isWarped (*c);
        const double bpb = beatsPerBar();

        // Griglia e righello (battute del tempo originale)
        const double step = gridStep();

        for (double b = std::floor (firstBeat / step) * step; xForBeat (b) < bounds.getRight(); b += step)
        {
            const float x = xForBeat (b);
            const bool bar = std::abs (std::fmod (b + 1.0e-6, bpb)) < 1.0e-3;
            g.setColour (bar ? colours::outline : colours::divider);
            g.fillRect (x, (float) rulerHeight, 1.0f, bounds.getHeight() - (float) rulerHeight);

            if (bar && b >= -1.0e-6)
            {
                g.setColour (colours::textDim);
                g.setFont (fonts::mono (10.5f));
                g.drawText (juce::String (juce::roundToInt (b / bpb) + 1), juce::Rectangle<float> (x + 3.0f, 0.0f, 40.0f, (float) rulerHeight),
                            juce::Justification::centredLeft, false);
            }
        }

        // La parte del file che la clip suona, il resto in ombra
        const auto used = usedBeats (*c);
        const float u0 = xForBeat (used.getStart()), u1 = xForBeat (used.getEnd());
        g.setColour (colour.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (u0, waveArea.getY(), u1, waveArea.getBottom()));

        // Forma d'onda, tratto per tratto tra i marker
        const auto markers = warp::getMarkers (*c);
        const double len = warp::sourceLength (*c);
        g.setColour (colour.brighter (0.25f));

        if (markers.size() >= 2)
        {
            for (size_t i = 0; i + 1 < markers.size(); ++i)
                drawSpan (g, *thumb, waveArea, xForBeat (markers[i].beat), xForBeat (markers[i + 1].beat),
                          markers[i].sourceSeconds, markers[i + 1].sourceSeconds);
        }
        else
        {
            drawSpan (g, *thumb, waveArea, xForBeat (0.0), xForBeat (fileBeats (*c)), 0.0, len);
        }

        g.setColour (colours::background.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bounds.getX(), waveArea.getY(), u0, waveArea.getBottom()));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (u1, waveArea.getY(), bounds.getRight(), waveArea.getBottom()));

        g.setColour (colour);
        g.fillRect (u0, waveArea.getY(), 1.5f, waveArea.getHeight());
        g.fillRect (u1 - 1.5f, waveArea.getY(), 1.5f, waveArea.getHeight());

        // Marker di warp: linea e maniglia nel righello
        if (warped)
        {
            for (size_t i = 0; i < markers.size(); ++i)
            {
                const float x = xForBeat (markers[i].beat);
                const bool hot = (int) i == dragging || (int) i == hovered;
                g.setColour (markerColour.withAlpha (hot ? 1.0f : 0.75f));
                g.fillRect (x - 0.5f, (float) rulerHeight, hot ? 2.0f : 1.0f, bounds.getHeight() - (float) rulerHeight);

                juce::Path handle;
                handle.addTriangle (x - 5.0f, 4.0f, x + 5.0f, 4.0f, x, (float) rulerHeight - 2.0f);
                g.fillPath (handle);
            }
        }

        if (owner.isFrozen())
            freezeui::drawFrozenVeil (g, bounds, 0.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int m = markerAt (e.x);

        if (m != hovered)
        {
            hovered = m;
            setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hovered >= 0)
        {
            hovered = -1;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        auto c = owner.clip;

        if (c == nullptr || owner.isFrozen())
            return;

        if (e.mods.isPopupMenu())
        {
            showMenu (e);
            return;
        }

        dragging = warp::isWarped (*c) ? markerAt (e.x) : -1;

        if (dragging >= 0)
            c->edit.getUndoManager().beginNewTransaction ("Sposta marker di warp");
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto c = owner.clip;

        if (c == nullptr || dragging < 0)
            return;

        double beat = beatForX ((float) e.x);

        if (! e.mods.isCommandDown())
        {
            const double step = gridStep() / 2.0;
            beat = std::round (beat / step) * step;
        }

        warp::moveMarker (*c, dragging, beat);
        owner.status ("Marker al beat " + juce::String (beat + 1.0, 2) + " (Ctrl: senza griglia)");
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragging = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto c = owner.clip;

        if (c == nullptr || owner.isFrozen())
            return;

        if (e.y < rulerHeight)
        {
            fit();
            return;
        }

        if (! warp::isWarped (*c))
        {
            owner.status ("Accendi il warp per usare i marker");
            return;
        }

        c->edit.getUndoManager().beginNewTransaction ("Marker di warp");

        if (const int m = markerAt (e.x); m >= 0)
        {
            warp::removeMarker (*c, m);
            owner.status ("Marker tolto");
        }
        else if (warp::addMarker (*c, warp::sourceForBeat (*c, beatForX ((float) e.x))) >= 0)
        {
            owner.status ("Marker aggiunto: trascinalo per stirare l'audio");
        }

        hovered = -1;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown())
        {
            const double anchor = beatForX ((float) e.x);
            pixelsPerBeat = juce::jlimit (1.0, 600.0, pixelsPerBeat * (w.deltaY > 0 ? 1.15 : 1.0 / 1.15));
            firstBeat = anchor - e.x / pixelsPerBeat;
        }
        else
        {
            const float delta = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
            firstBeat -= delta * 120.0 / pixelsPerBeat;
        }

        repaint();
    }

    juce::String hintAt (juce::Point<int> p)
    {
        auto c = owner.clip;

        if (c == nullptr)
            return {};

        if (p.y < rulerHeight)
            return "Righello: doppio clic per vedere tutto il file";

        if (! warp::isWarped (*c))
            return "Forma d'onda: accendi il warp per agganciare l'audio alle battute e usare i marker";

        if (markerAt (p.x) >= 0)
            return juce::String::fromUTF8 ("Marker di warp: trascina per stirare l'audio \xc2\xb7 Ctrl senza griglia \xc2\xb7 doppio clic lo toglie");

        return juce::String::fromUTF8 ("Forma d'onda: doppio clic aggiunge un marker \xc2\xb7 Ctrl+rotella zoom \xc2\xb7 clic destro per il menu");
    }

private:
    AudioClipEditor& owner;
    std::unique_ptr<te::SmartThumbnail> thumb;
    std::unique_ptr<anim::PlayheadOverlay> playhead;
    double pixelsPerBeat = 40.0, firstBeat = 0.0;
    int dragging = -1, hovered = -1;
    bool fitted = false;

    float xForBeat (double b) const     { return (float) ((b - firstBeat) * pixelsPerBeat); }
    double beatForX (float x) const     { return firstBeat + x / pixelsPerBeat; }

    double beatsPerBar() const
    {
        auto c = owner.clip;
        return c != nullptr ? (double) c->edit.tempoSequence.getTimeSigAt (c->getPosition().getStart()).numerator.get() : 4.0;
    }

    /** Il passo della griglia secondo lo zoom: almeno ~12 px. */
    double gridStep() const
    {
        double step = 0.25;

        while (step * pixelsPerBeat < 12.0)
            step *= 2.0;

        return step;
    }

    static double fileBeats (const te::AudioClipBase& c)
    {
        const auto markers = warp::getMarkers (c);

        if (! markers.empty())
            return markers.back().beat;

        return warp::beatForSource (c, warp::sourceLength (c));
    }

    /** Il tratto del file che la clip suona, in beat del file. */
    static juce::Range<double> usedBeats (const te::AudioClipBase& c)
    {
        const auto pos = c.getPosition();
        const auto first = warp::sourceSecondsAt (c, pos.getStart());

        if (! first)
            return {};

        if (c.isLooping())
        {
            if (c.getAutoTempo())
            {
                const auto loop = c.getLoopRangeBeats();
                return { loop.getStart().inBeats(), loop.getEnd().inBeats() };
            }

            const auto loop = c.getLoopRange();
            return { warp::beatForSource (c, loop.getStart().inSeconds() * c.getSpeedRatio()),
                     warp::beatForSource (c, loop.getEnd().inSeconds() * c.getSpeedRatio()) };
        }

        const auto last = warp::sourceSecondsAt (c, pos.getEnd());
        return { warp::beatForSource (c, *first), warp::beatForSource (c, last.value_or (warp::sourceLength (c))) };
    }

    std::optional<double> playBeat() const
    {
        auto c = owner.clip;

        if (c == nullptr)
            return std::nullopt;

        const auto now = c->edit.getTransport().getPosition();

        if (auto source = warp::sourceSecondsAt (*c, now))
            return warp::beatForSource (*c, *source);

        return std::nullopt;
    }

    int markerAt (int x) const
    {
        auto c = owner.clip;

        if (c == nullptr || ! warp::isWarped (*c))
            return -1;

        const auto markers = warp::getMarkers (*c);
        int best = -1;
        float bestDistance = (float) markerGrab + 0.5f;

        for (size_t i = 0; i < markers.size(); ++i)
        {
            const float d = std::abs (xForBeat (markers[i].beat) - (float) x);

            if (d < bestDistance)
            {
                bestDistance = d;
                best = (int) i;
            }
        }

        return best;
    }

    void showMenu (const juce::MouseEvent& e)
    {
        auto c = owner.clip;
        const int m = markerAt (e.x);
        const double beat = beatForX ((float) e.x);
        const int numMarkers = (int) warp::getMarkers (*c).size();
        juce::PopupMenu menu;

        menu.addItem ("Aggiungi un marker qui", warp::isWarped (*c) && m < 0, false, [this, beat]
        {
            if (auto clipNow = owner.clip)
            {
                clipNow->edit.getUndoManager().beginNewTransaction ("Marker di warp");
                warp::addMarker (*clipNow, warp::sourceForBeat (*clipNow, beat));
                repaint();
            }
        });

        menu.addItem ("Togli questo marker", m > 0 && m < numMarkers - 1, false, [this, m]
        {
            if (auto clipNow = owner.clip)
            {
                clipNow->edit.getUndoManager().beginNewTransaction ("Togli marker");
                warp::removeMarker (*clipNow, m);
                repaint();
            }
        });

        menu.addItem ("Togli tutti i marker", numMarkers > 0, false, [this]
        {
            if (auto clipNow = owner.clip)
            {
                clipNow->edit.getUndoManager().beginNewTransaction ("Togli i marker");
                warp::resetMarkers (*clipNow);
                repaint();
            }
        });

        menu.addSeparator();
        menu.addItem ("Adatta la vista", [this] { fit(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
    }
};

//==============================================================================
AudioClipEditor::AudioClipEditor()
{
    wave = std::make_unique<WaveView> (*this);
    addAndMakeVisible (*wave);

    for (auto* b : { &warpButton, &modeButton, &halfButton, &doubleButton, &reverseButton, &resetButton })
    {
        b->setBaseColour (colours::panel);
        addAndMakeVisible (b);
    }

    for (auto* n : { &tempo, &transpose, &detune, &gain })
        addAndMakeVisible (n);

    warpButton.setOnColours (colours::accent, colours::ink);
    reverseButton.setOnColours (colours::accent, colours::ink);

    warpButton.setTooltip ("Aggancia l'audio alle battute del progetto: segue il tempo senza cambiare altezza");
    modeButton.setTooltip ("Come allungare l'audio");
    halfButton.setTooltip ("Tempo originale a meta' (la clip occupa meta' delle battute)");
    doubleButton.setTooltip ("Tempo originale doppio (la clip occupa il doppio delle battute)");
    reverseButton.setTooltip ("Suona il file al contrario");
    resetButton.setTooltip ("Toglie i marker di warp: lo stretch torna uniforme");

    auto edit = [this] (const juce::String& name, std::function<void (te::WaveAudioClip&)> fn)
    {
        if (clip == nullptr)
            return;

        if (isFrozen())
        {
            status (freezeui::frozenNotice (*dynamic_cast<te::AudioTrack*> (clip->getTrack())));
            refreshControls();
            return;
        }

        clip->edit.getUndoManager().beginNewTransaction (name);
        fn (*clip);
        refreshControls();
        wave->repaint();
    };

    warpButton.onClick = [this, edit]
    {
        edit ("Warp", [this] (te::WaveAudioClip& c)
        {
            warp::setWarped (c, ! warp::isWarped (c));
            status (warp::isWarped (c) ? "Warp acceso: la clip segue il tempo del progetto"
                                       : "Warp spento: la clip suona alla sua velocita'");
        });
    };

    modeButton.onClick = [this] { showModeMenu(); };
    halfButton.onClick = [edit] { edit ("Tempo originale", [] (te::WaveAudioClip& c) { warp::setOriginalTempo (c, warp::getOriginalTempo (c) / 2.0); }); };
    doubleButton.onClick = [edit] { edit ("Tempo originale", [] (te::WaveAudioClip& c) { warp::setOriginalTempo (c, warp::getOriginalTempo (c) * 2.0); }); };
    reverseButton.onClick = [edit] { edit ("Al contrario", [] (te::WaveAudioClip& c) { c.setIsReversed (! c.getIsReversed()); }); };
    resetButton.onClick = [edit] { edit ("Togli i marker", [] (te::WaveAudioClip& c) { warp::resetMarkers (c); }); };

    tempo.onChange = [edit] (double v) { edit ("Tempo originale", [v] (te::WaveAudioClip& c) { warp::setOriginalTempo (c, v); }); };
    transpose.onChange = [edit] (double v) { edit ("Trasposizione", [v] (te::WaveAudioClip& c) { warp::setTranspose (c, juce::roundToInt (v)); }); };
    detune.onChange = [edit] (double v) { edit ("Accordatura fine", [v] (te::WaveAudioClip& c) { warp::setDetune (c, juce::roundToInt (v)); }); };
    gain.onChange = [edit] (double v) { edit ("Volume della clip", [v] (te::WaveAudioClip& c) { c.setGainDB ((float) v); }); };

    startTimerHz (8);
}

AudioClipEditor::~AudioClipEditor() = default;

void AudioClipEditor::setClip (te::WaveAudioClip* c)
{
    if (c == clip.get())
        return;

    clip = c;
    wave->setClip (c);
    refreshControls();
    repaint();
}

void AudioClipEditor::refreshControls()
{
    const bool has = clip != nullptr;

    for (auto* comp : std::initializer_list<juce::Component*> { &warpButton, &modeButton, &halfButton, &doubleButton,
                                                                 &reverseButton, &resetButton, &tempo, &transpose, &detune, &gain })
        comp->setEnabled (has && ! isFrozen());

    if (! has)
        return;

    const bool warped = warp::isWarped (*clip);
    warpButton.setToggleState (warped, juce::dontSendNotification);
    modeButton.setButtonText (warp::name (warp::getMode (*clip)) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    modeButton.setEnabled (warped && ! isFrozen());
    reverseButton.setToggleState (clip->getIsReversed(), juce::dontSendNotification);
    resetButton.setEnabled (warped && ! warp::getMarkers (*clip).empty() && ! isFrozen());

    tempo.setValue (warp::getOriginalTempo (*clip), juce::dontSendNotification);
    transpose.setValue (warp::getTranspose (*clip), juce::dontSendNotification);
    detune.setValue (warp::getDetune (*clip), juce::dontSendNotification);
    gain.setValue (clip->getGainDB(), juce::dontSendNotification);
}

void AudioClipEditor::showModeMenu()
{
    if (clip == nullptr)
        return;

    juce::PopupMenu m;
    const auto current = warp::getMode (*clip);

    auto describe = [] (warp::Mode mode) -> juce::String
    {
        switch (mode)
        {
            case warp::Mode::complete:  return "Completo: alta qualita', per quasi tutto";
            case warp::Mode::light:     return "Leggero: meno CPU, un po' meno qualita'";
            case warp::Mode::repitch:   return "Ripitch: cambia velocita' e altezza insieme, come un giradischi";
        }

        return {};
    };

    for (auto mode : warp::allModes())
        m.addItem (describe (mode), true, mode == current, [this, mode]
        {
            if (clip == nullptr || isFrozen())
                return;

            clip->edit.getUndoManager().beginNewTransaction ("Modo di warp");
            warp::setMode (*clip, mode);
            refreshControls();
            status ("Warp: " + warp::name (mode));
        });

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modeButton));
}

bool AudioClipEditor::isFrozen() const
{
    auto t = clip != nullptr ? dynamic_cast<te::AudioTrack*> (clip->getTrack()) : nullptr;
    return t != nullptr && freeze::isFrozen (*t);
}

void AudioClipEditor::status (const juce::String& s)
{
    if (onStatus)
        onStatus (s);
}

void AudioClipEditor::timerCallback()
{
    // La clip puo' cambiare da fuori (undo, arrangement, tempo del progetto)
    if (clip != nullptr && clip->state.getParent().isValid())
    {
        if (! tempo.isMouseButtonDown() && ! transpose.isMouseButtonDown()
            && ! detune.isMouseButtonDown() && ! gain.isMouseButtonDown())
            refreshControls();

        wave->repaint();
    }
    else if (clip != nullptr)
    {
        setClip (nullptr);
    }
}

void AudioClipEditor::paint (juce::Graphics& g)
{
    g.setColour (colours::textDim);
    g.setFont (fonts::ui (11.5f));

    const char* labels[] = { "", "Tempo", "Trasposizione", "Fine", "Volume" };
    auto r = labelsArea;

    for (auto* label : labels)
    {
        g.drawText (juce::String::fromUTF8 (label), r.removeFromTop (rowHeight), juce::Justification::centredLeft, false);
        r.removeFromTop (rowGap);
    }

    if (clip == nullptr)
    {
        g.setColour (colours::textDim);
        g.drawText ("Seleziona una clip audio", wave->getBounds(), juce::Justification::centred, false);
    }
}

void AudioClipEditor::resized()
{
    auto r = getLocalBounds();
    auto controls = r.removeFromLeft (controlsWidth).reduced (0, 2);
    r.removeFromLeft (12);
    wave->setBounds (r);

    labelsArea = controls.withWidth (labelWidth);

    auto row = [&controls]
    {
        auto line = controls.removeFromTop (rowHeight);
        controls.removeFromTop (rowGap);
        return line;
    };

    auto line = row();
    warpButton.setBounds (line.removeFromLeft (70));
    line.removeFromLeft (6);
    modeButton.setBounds (line);

    line = row().withTrimmedLeft (labelWidth);
    doubleButton.setBounds (line.removeFromRight (30));
    line.removeFromRight (4);
    halfButton.setBounds (line.removeFromRight (30));
    line.removeFromRight (4);
    tempo.setBounds (line);

    transpose.setBounds (row().withTrimmedLeft (labelWidth));
    detune.setBounds (row().withTrimmedLeft (labelWidth));
    gain.setBounds (row().withTrimmedLeft (labelWidth));

    line = row();
    reverseButton.setBounds (line.removeFromLeft ((line.getWidth() - 6) / 2));
    line.removeFromLeft (6);
    resetButton.setBounds (line);
}

juce::String AudioClipEditor::getHintAt (juce::Point<int> p)
{
    if (wave->getBounds().contains (p))
        return wave->hintAt (p - wave->getPosition());

    if (tempo.getBounds().expanded (labelWidth, 0).contains (p) && p.y >= tempo.getY() && p.y < tempo.getBottom())
        return "Tempo originale del file: trascina in verticale o doppio clic per scriverlo";

    if (transpose.getBounds().getUnion (detune.getBounds()).withX (0).withWidth (controlsWidth).contains (p))
        return "Trasposizione in semitoni e accordatura fine in cent (con il warp non cambia la durata)";

    return {};
}

} // namespace sonora
