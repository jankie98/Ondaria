/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "ArrangementView.h"
#include "Animation.h"
#include "FreezeUI.h"
#include "RecordingUI.h"
#include "session/Arrangement.h"
#include "session/Automation.h"

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
    constexpr int automationHeight = 52;
    constexpr int pointRadius = 4;
    constexpr int valueInset = 6;

    bool isOnFrozenTrack (te::Clip& c)
    {
        auto t = dynamic_cast<te::AudioTrack*> (c.getTrack());
        return t != nullptr && freeze::isFrozen (*t);
    }

    namespace au = sonora::automation;

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
    backButton.setOnColours (colours::warning, colours::ink);
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

int ArrangementView::trackBlockHeight (const te::AudioTrack& t) const
{
    return laneHeight + laneGap + au::getShownLanes (t).size() * (automationHeight + laneGap);
}

int ArrangementView::laneTop (int index) const
{
    int y = lanesArea().getY() - scrollY;

    if (edit != nullptr)
    {
        auto tracks = te::getAudioTracks (*edit);

        for (int i = 0; i < index && i < tracks.size(); ++i)
            y += trackBlockHeight (*tracks[i]);
    }

    return y;
}

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

//==============================================================================
std::vector<ArrangementView::AutomationLane> ArrangementView::automationLanes (te::AudioTrack& t, int trackIndex) const
{
    std::vector<AutomationLane> result;
    int y = laneTop (trackIndex) + laneHeight + laneGap;
    const auto lanes = lanesArea();

    for (auto& key : au::getShownLanes (t))
    {
        AutomationLane l;
        l.track = &t;
        l.key = key;
        l.param = au::findParameter (t, key);
        l.header = { padding, y, headerWidth, automationHeight };
        l.lane = { lanes.getX(), y, lanes.getWidth(), automationHeight };
        result.push_back (std::move (l));
        y += automationHeight + laneGap;
    }

    return result;
}

std::optional<ArrangementView::AutomationLane> ArrangementView::automationLaneAt (juce::Point<int> p) const
{
    if (edit == nullptr || p.y < lanesArea().getY())
        return std::nullopt;

    auto tracks = te::getAudioTracks (*edit);
    int top = lanesArea().getY() - scrollY;

    for (int i = 0; i < tracks.size(); ++i)
    {
        const int height = trackBlockHeight (*tracks[i]);

        if (p.y >= top + laneHeight && p.y < top + height)
            for (auto& l : automationLanes (*tracks[i], i))
                if (p.y >= l.lane.getY() && p.y < l.lane.getBottom())
                    return l;

        top += height;
    }

    return std::nullopt;
}

int ArrangementView::xForTime (te::TimePosition t) const
{
    return xForBeat (edit->tempoSequence.toBeats (t).inBeats());
}

te::TimePosition ArrangementView::timeForX (int x, bool snapToGrid) const
{
    const double beat = juce::jmax (0.0, beatForX (x));
    return edit->tempoSequence.toTime (snapToGrid ? snap (beat) : te::BeatPosition::fromBeats (beat));
}

float ArrangementView::yForValue (juce::Rectangle<int> lane, float normalised) const
{
    const float h = (float) (lane.getHeight() - 2 * valueInset);
    return (float) (lane.getBottom() - valueInset) - normalised * h;
}

float ArrangementView::valueForY (juce::Rectangle<int> lane, int y) const
{
    const float h = (float) (lane.getHeight() - 2 * valueInset);
    return juce::jlimit (0.0f, 1.0f, ((float) (lane.getBottom() - valueInset) - (float) y) / h);
}

int ArrangementView::automationPointAt (const AutomationLane& l, juce::Point<int> p) const
{
    if (l.param == nullptr)
        return -1;

    auto& curve = l.param->getCurve();
    int best = -1;
    float bestDistance = (float) pointRadius + 4.0f;

    for (int i = 0; i < curve.getNumPoints(); ++i)
    {
        const juce::Point<float> pt ((float) xForTime (curve.getPointTime (i)),
                                     yForValue (l.lane, au::toNormalised (*l.param, curve.getPointValue (i))));
        const float d = pt.getDistanceFrom (p.toFloat());

        if (d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }

    return best;
}

juce::Rectangle<int> ArrangementView::hideButtonArea (const AutomationLane& l) const
{
    return { l.header.getRight() - 24, l.header.getY() + 6, 16, 16 };
}

juce::Rectangle<int> ArrangementView::automationButtonArea (int laneIndex) const
{
    return { padding + headerWidth - 56, laneTop (laneIndex) + 8, 22, 22 };
}

void ArrangementView::drawAutomationLane (juce::Graphics& g, const AutomationLane& l, juce::Colour colour, bool selected)
{
    const auto mouse = getMouseXYRelative();
    const bool hover = isMouseOver() && l.lane.contains (mouse);

    // Intestazione: nome del parametro, valore attuale, chiudi
    g.setColour (colours::panel.withAlpha (selected ? 0.95f : 0.7f));
    g.fillRoundedRectangle (l.header.toFloat(), metrics::radius);
    g.setColour (colour.withAlpha (0.55f));
    g.fillRect (l.header.getX() + 1, l.header.getY() + 6, 2, l.header.getHeight() - 12);

    auto text = l.header.reduced (12, 6).withTrimmedRight (22);
    g.setColour (l.param != nullptr ? colours::textMid : colours::textDim);
    g.setFont (fonts::ui (11.0f, fonts::Weight::medium));
    g.drawText (l.param != nullptr ? au::displayName (*l.param) : juce::String ("Parametro non disponibile"),
                text.removeFromTop (16), juce::Justification::centredLeft, true);

    if (l.param != nullptr)
    {
        g.setColour (colours::textDim);
        g.setFont (fonts::mono (10.0f));
        g.drawText (au::valueText (*l.param, l.param->getCurrentValue()), text.removeFromBottom (14),
                    juce::Justification::centredLeft, true);
    }

    const auto close = hideButtonArea (l).toFloat();
    const bool closeHover = isMouseOver() && close.expanded (3.0f).contains (mouse.toFloat());
    g.setColour (closeHover ? colours::text : colours::textDim);
    g.drawLine (close.getX() + 4.0f, close.getY() + 4.0f, close.getRight() - 4.0f, close.getBottom() - 4.0f, 1.3f);
    g.drawLine (close.getRight() - 4.0f, close.getY() + 4.0f, close.getX() + 4.0f, close.getBottom() - 4.0f, 1.3f);

    // Corsia
    g.setColour (colours::emptySlot.withAlpha (0.35f));
    g.fillRoundedRectangle (l.lane.toFloat(), metrics::radius);

    if (l.param == nullptr)
        return;

    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (l.lane.getIntersection (lanesArea()));

    // Riferimenti a meta' e agli estremi
    g.setColour (colours::divider.withAlpha (0.6f));
    g.fillRect (l.lane.getX(), juce::roundToInt (yForValue (l.lane, 0.5f)), l.lane.getWidth(), 1);

    auto& curve = l.param->getCurve();
    const int numPoints = curve.getNumPoints();

    if (numPoints == 0)
    {
        const float y = yForValue (l.lane, au::normalisedValueAt (*l.param, {}));
        g.setColour (colour.withAlpha (0.35f));
        g.drawHorizontalLine (juce::roundToInt (y), (float) l.lane.getX(), (float) l.lane.getRight());
        g.setColour (colours::textDim.withAlpha (hover ? 0.9f : 0.5f));
        g.setFont (fonts::ui (10.5f));
        g.drawText ("Clic per aggiungere un punto", l.lane.reduced (10, 4), juce::Justification::topLeft);
        return;
    }

    // La curva, campionata ogni due pixel (segue anche i tratti curvati)
    juce::Path line, fill;
    const int x1 = l.lane.getX(), x2 = l.lane.getRight();

    for (int x = x1; x <= x2 + 1; x += 2)
    {
        const auto t = timeForX (x, false);
        const float y = yForValue (l.lane, au::normalisedValueAt (*l.param, t));

        if (x == x1)
        {
            line.startNewSubPath ((float) x, y);
            fill.startNewSubPath ((float) x, (float) l.lane.getBottom());
        }
        else
        {
            line.lineTo ((float) x, y);
        }

        fill.lineTo ((float) x, y);
    }

    fill.lineTo ((float) x2 + 1.0f, (float) l.lane.getBottom());
    fill.closeSubPath();

    g.setColour (colour.withAlpha (selected ? 0.16f : 0.1f));
    g.fillPath (fill);
    g.setColour (colour.brighter (0.2f));
    g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const int hovered = hover ? automationPointAt (l, mouse) : -1;
    const bool dragging = drag == Drag::point && dragParam == l.param;

    for (int i = 0; i < numPoints; ++i)
    {
        const float x = (float) xForTime (curve.getPointTime (i));

        if (x < (float) x1 - 10.0f || x > (float) x2 + 10.0f)
            continue;

        const float y = yForValue (l.lane, au::toNormalised (*l.param, curve.getPointValue (i)));
        const bool active = i == hovered || (dragging && i == dragPoint);
        const float r = active ? (float) pointRadius + 1.5f : (float) pointRadius;
        g.setColour (colours::background);
        g.fillEllipse (x - r - 1.5f, y - r - 1.5f, (r + 1.5f) * 2.0f, (r + 1.5f) * 2.0f);
        g.setColour (active ? colours::text : colour.brighter (0.4f));
        g.fillEllipse (x - r, y - r, r * 2.0f, r * 2.0f);

        // Valore accanto al punto che si sta trascinando o indicando
        if (active)
        {
            const auto label = au::valueText (*l.param, curve.getPointValue (i));
            g.setFont (fonts::mono (10.0f));
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 10;
            auto box = juce::Rectangle<float> (x + 8.0f, y - 9.0f, (float) w, 16.0f);

            if (box.getRight() > (float) x2)
                box.setX (x - 8.0f - (float) w);

            box.setY (juce::jlimit ((float) l.lane.getY() + 1.0f, (float) l.lane.getBottom() - 17.0f, box.getY()));
            g.setColour (colours::panelLight);
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (colours::text);
            g.drawText (label, box, juce::Justification::centred);
        }
    }
}

juce::PopupMenu ArrangementView::automationMenu (te::AudioTrack& track)
{
    juce::PopupMenu m;
    te::AudioTrack::Ptr t (&track);
    juce::Component::SafePointer<ArrangementView> safe (this);
    auto refresh = [safe] { if (safe != nullptr) safe->repaint(); };

    auto addParam = [&] (juce::PopupMenu& menu, const au::ParameterInfo& info, const juce::String& label)
    {
        const bool shown = au::isLaneShown (track, info.key);
        // Un pallino accanto ai parametri che hanno gia' dei punti
        const auto text = au::hasAutomation (*info.param) ? label + juce::String (juce::CharPointer_UTF8 ("  \xe2\x80\xa2")) : label;
        menu.addItem (text, true, shown, [t, key = info.key, shown, refresh]
        {
            au::setLaneShown (*t, key, ! shown);
            refresh();
        });
    };

    // Il mixer in cima, poi un sottomenu per ogni device
    std::vector<std::pair<juce::String, juce::PopupMenu>> devices;

    for (auto& info : au::getParameters (track))
    {
        auto plugin = info.param->getPlugin();

        if (dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr)
        {
            addParam (m, info, info.param->getParameterName());
            continue;
        }

        const auto owner = plugin != nullptr ? plugin->getName() : juce::String ("Altro");

        if (devices.empty() || devices.back().first != owner)
            devices.push_back ({ owner, {} });

        addParam (devices.back().second, info, info.param->getParameterName());
    }

    if (! devices.empty())
    {
        m.addSeparator();

        for (auto& [name, sub] : devices)
            m.addSubMenu (name, sub);
    }

    m.addSeparator();
    m.addItem ("Mostra quelli con automazione", au::hasAutomation (track), false, [t, refresh]
    {
        au::showAutomatedLanes (*t);
        refresh();
    });
    m.addItem ("Nascondi tutte le corsie", ! au::getShownLanes (track).isEmpty(), false, [t, refresh]
    {
        au::hideAllLanes (*t);
        refresh();
    });
    return m;
}

void ArrangementView::showLaneMenu (const AutomationLane& l, int pointIndex)
{
    juce::PopupMenu m;
    te::AudioTrack::Ptr t (l.track);
    auto param = l.param;
    const auto key = l.key;
    juce::Component::SafePointer<ArrangementView> safe (this);
    auto refresh = [safe] { if (safe != nullptr) safe->repaint(); };

    if (param != nullptr)
    {
        if (pointIndex >= 0)
        {
            m.addItem ("Elimina il punto", [param, pointIndex, refresh]
            {
                param->getEdit().getUndoManager().beginNewTransaction ("Elimina punto");
                au::removePoint (*param, pointIndex);
                refresh();
            });

            if (pointIndex < param->getCurve().getNumPoints() - 1)
                m.addItem ("Raddrizza il tratto", std::abs (param->getCurve().getPointCurve (pointIndex)) > 1.0e-4f, false,
                           [param, pointIndex, refresh]
                {
                    param->getEdit().getUndoManager().beginNewTransaction ("Raddrizza tratto");
                    au::setCurve (*param, pointIndex, 0.0f);
                    refresh();
                });

            m.addSeparator();
        }

        m.addItem ("Cancella tutta l'automazione", au::hasAutomation (*param), false, [param, refresh]
        {
            param->getEdit().getUndoManager().beginNewTransaction ("Cancella automazione");
            au::clear (*param);
            refresh();
        });
    }

    m.addItem ("Nascondi la corsia", [t, key, refresh]
    {
        au::setLaneShown (*t, key, false);
        refresh();
    });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

te::BeatPosition ArrangementView::snap (double beat) const
{
    const double step = gridBeats();
    return te::BeatPosition::fromBeats (juce::jmax (0.0, std::round (beat / step) * step));
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

    updateSnapButton();

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

    // Linee della griglia sotto le battute: beat e suddivisioni compaiono ingrandendo
    {
        const double step = std::min (gridBeats(), 1.0);

        if (step * pixelsPerBeat >= 8.0 && step < (double) beatsPerBar)
        {
            const double first = std::floor (viewStart / step) * step;

            for (double b = first; ; b += step)
            {
                const int x = xForBeat (b);

                if (x > lanes.getRight())
                    break;

                if (x < lanes.getX())
                    continue;

                const bool onBeat = std::abs (b - std::round (b)) < 1.0e-6;
                g.setColour (colours::divider.withAlpha (onBeat ? 0.9f : 0.45f));
                g.fillRect (x, lanes.getY(), 1, lanes.getHeight());

                // Etichette dei beat (3.2, 3.3...) quando c'e' spazio
                if (onBeat && pixelsPerBeat >= 44.0 && (int) std::round (b) % beatsPerBar != 0)
                {
                    const int bar = (int) std::floor (std::round (b) / beatsPerBar);
                    const int beat = (int) std::round (b) % beatsPerBar;
                    g.setColour (colours::textDim.withAlpha (0.6f));
                    g.setFont (fonts::mono (9.0f));
                    g.drawText (juce::String (bar + 1) + "." + juce::String (beat + 1), x + 3, ruler.getY(), 40,
                                ruler.getHeight(), juce::Justification::centredLeft);
                }
            }
        }
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

        if (x < lanes.getX())
            continue;

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

        if (y + trackBlockHeight (*t) < lanes.getY() || y > lanes.getBottom())
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

        const bool frozen = freeze::isFrozen (*t);
        auto text = header.reduced (12, 0).withTrimmedRight (frozen ? 80 : 60).toFloat();
        g.setColour (colours::text);
        g.setFont (fonts::ui (12.5f, fonts::Weight::medium));
        g.drawText (t->getName(), text, juce::Justification::centredLeft, true);

        if (frozen)
            freezeui::drawSnowflake (g, automationButtonArea (i).toFloat().translated (-22.0f, 0.0f).reduced (4.0f),
                                     freezeui::iceColour());

        if (recorder != nullptr)
        {
            const bool armed = recorder->isArmed (*t);
            const bool hover = isMouseOver() && armArea (i).contains (getMouseXYRelative());
            recordingui::drawArmButton (g, armArea (i).toFloat(), armed, armed && ! recording::isExplicitlyArmed (*t), hover);

            g.setColour (colours::textDim);
            g.setFont (fonts::ui (10.5f));
            g.drawText (recording::getInputName (*t), header.reduced (10, 7), juce::Justification::bottomLeft, true);
        }

        // Pulsante delle corsie di automazione: acceso se la traccia ne mostra o ne ha
        {
            const auto area = automationButtonArea (i).toFloat();
            const bool hover = isMouseOver() && area.contains (getMouseXYRelative().toFloat());
            const bool lit = ! au::getShownLanes (*t).isEmpty();

            if (hover || lit)
            {
                g.setColour (lit ? colour.withAlpha (0.25f) : colours::hover);
                g.fillRoundedRectangle (area, 5.0f);
            }

            icons::draw (g, icons::automation(), area.reduced (4.0f),
                         lit ? colour.brighter (0.4f) : (au::hasAutomation (*t) ? colours::textMid : colours::textDim));
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
                    g.setColour (colours::ink);
                    g.setFont (fonts::ui (10.5f, fonts::Weight::medium));
                    g.drawText ("Registrazione", rec.withHeight (16.0f).reduced (6.0f, 0.0f).withY (rec.getY() - 16.0f),
                                juce::Justification::centredLeft, true);

                    // Quello che sta arrivando: forma d'onda o note, mentre lo suoni
                    if (auto take = recorder->getLiveTake (*t))
                        recordingui::drawLiveTake (g, *take, rec.reduced (2.0f, 3.0f), transport.getPosition(),
                                                   [this, &ts] (te::TimePosition time)
                                                   { return (float) xForBeat (ts.toBeats (time).inBeats()); },
                                                   colours::record.brighter (0.35f));
                }
            }
        }

        g.restoreState();

        // Corsie di automazione sotto la traccia
        for (auto& l : automationLanes (*t, i))
        {
            if (l.lane.getBottom() < lanes.getY() || l.lane.getY() > lanes.getBottom())
                continue;

            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (getLocalBounds().withTop (lanes.getY()).withBottom (lanes.getBottom()));
            drawAutomationLane (g, l, colour, selected);
        }
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

    // Zoom semantico: molto lontano una clip e' solo un blocco di colore
    if (r.getWidth() < 24)
    {
        g.setColour (colour.withAlpha (0.75f));
        g.fillRoundedRectangle (r.toFloat(), 3.0f);

        if (isOnFrozenTrack (c))
            freezeui::drawFrozenVeil (g, r.toFloat(), 3.0f);

        if (selected)
        {
            g.setColour (colours::text);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, 1.5f);
        }

        return;
    }

    g.setColour (colour.withAlpha (0.28f));
    g.fillRoundedRectangle (r.toFloat(), 5.0f);
    auto title = r.removeFromTop (16);
    g.setColour (colour);
    g.fillRoundedRectangle (title.toFloat(), 5.0f);
    g.fillRect (title.withTrimmedTop (8));

    if (title.getWidth() >= 36)
    {
        g.setColour (colours::ink);
        g.setFont (fonts::ui (11.0f, fonts::Weight::medium));
        g.drawText (c.getName(), title.reduced (6, 0), juce::Justification::centredLeft, true);
    }

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

            // Ingrandendo le note diventano piu' spesse e mostrano la velocity
            const bool detailed = pixelsPerBeat >= 48.0;
            const float noteH = detailed ? juce::jlimit (2.0f, 6.0f, (float) body.getHeight() / (float) range) : 2.0f;

            for (auto n : notes)
            {
                const double s = (n->getStartBeat().inBeats() - offset) / length;
                const double e = (n->getEndBeat().inBeats() - offset) / length;

                if (e <= 0.0 || s >= 1.0)
                    continue;

                const float x1 = (float) body.getX() + (float) body.getWidth() * (float) juce::jmax (0.0, s);
                const float x2 = (float) body.getX() + (float) body.getWidth() * (float) juce::jmin (1.0, e);
                const float y = (float) body.getBottom() - (float) body.getHeight() * (float) (n->getNoteNumber() - lo + 1) / (float) range;

                if (detailed)
                {
                    g.setColour (colour.brighter (0.4f).withAlpha (0.45f + 0.55f * (float) n->getVelocity() / 127.0f));
                    g.fillRoundedRectangle (x1, y, juce::jmax (2.0f, x2 - x1 - 1.0f), noteH, 1.0f);
                }
                else
                {
                    g.fillRect (x1, y, juce::jmax (1.5f, x2 - x1), noteH);
                }
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

    if (isOnFrozenTrack (c))
        freezeui::drawFrozenVeil (g, clipRect (c).toFloat(), 5.0f);

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

    // Mentre si scrivono automazioni, si vede subito la corsia del parametro che si muove
    if (transport.isPlaying())
        au::showLanesBeingWritten (*edit);

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
    juce::PopupMenu m;
    te::AudioTrack::Ptr t (&track);

    if (recorder != nullptr)
    {
        juce::WeakReference<recording::Recorder> rec (recorder);
        m.addSubMenu ("Ingresso", recordingui::inputMenu (*recorder, track));
        m.addItem ("Arma per la registrazione", true, recording::isExplicitlyArmed (track), [t, rec]
        {
            if (rec != nullptr)
                rec->setArmed (*t, ! recording::isExplicitlyArmed (*t));
        });
        m.addSeparator();
    }

    m.addSubMenu ("Automazione", automationMenu (track));
    m.addSeparator();
    freezeui::addMenuItems (m, track, [safe = juce::Component::SafePointer<ArrangementView> (this)] (const juce::String& s)
    {
        if (safe != nullptr)
        {
            safe->repaint();

            if (safe->onStatus)
                safe->onStatus (s);
        }
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

    // Corsie di automazione
    if (auto l = automationLaneAt (pos))
    {
        if (onTrackSelected)
            onTrackSelected (l->track);

        if (pos.x < lanesArea().getX())
        {
            if (e.mods.isPopupMenu())
                showLaneMenu (*l, -1);
            else if (hideButtonArea (*l).expanded (3).contains (pos))
                au::setLaneShown (*l->track, l->key, false);

            repaint();
            return;
        }

        if (l->param == nullptr)
            return;

        const int index = automationPointAt (*l, pos);

        if (e.mods.isPopupMenu())
        {
            showLaneMenu (*l, index);
            return;
        }

        auto& curve = l->param->getCurve();
        edit->getUndoManager().beginNewTransaction ("Automazione");
        dragParam = l->param;
        dragLane = l->lane;
        dragStartY = pos.y;

        if (index >= 0)
        {
            dragPoint = index;
            drag = Drag::point;
        }
        else if (e.mods.isAltDown())
        {
            // Alt: curva il tratto sotto il mouse
            const auto t = timeForX (pos.x, false);
            dragPoint = -1;

            for (int i = 0; i < curve.getNumPoints() - 1; ++i)
                if (curve.getPointTime (i) <= t && curve.getPointTime (i + 1) > t)
                    dragPoint = i;

            if (dragPoint >= 0)
            {
                dragCurveStart = curve.getPointCurve (dragPoint);
                drag = Drag::curve;
            }
        }
        else
        {
            dragPoint = au::addPoint (*l->param, timeForX (pos.x, ! e.mods.isCommandDown()), valueForY (l->lane, pos.y));
            drag = Drag::point;
        }

        repaint();
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
            else if (automationButtonArea (index).expanded (2).contains (pos) && ! e.mods.isPopupMenu())
            {
                if (onTrackSelected)
                    onTrackSelected (t);

                automationMenu (*t).showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (
                    localAreaToGlobal (automationButtonArea (index))));
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

    // Le clip di una traccia congelata non si toccano: suona il file del freeze
    if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack()); t != nullptr && freeze::isFrozen (*t))
    {
        if (e.mods.isPopupMenu())
            showHeaderMenu (*t);
        else if (onStatus)
            onStatus (freezeui::frozenNotice (*t));

        return;
    }

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

    if (drag == Drag::point && dragParam != nullptr)
    {
        dragPoint = au::movePoint (*dragParam, dragPoint, timeForX (e.x, ! e.mods.isCommandDown()), valueForY (dragLane, e.y));
        repaint();
        return;
    }

    if (drag == Drag::curve && dragParam != nullptr)
    {
        // Trascinando in su il tratto si gonfia verso l'alto. Nel motore una curvatura
        // positiva abbassa i tratti in salita e alza quelli in discesa.
        auto& curve = dragParam->getCurve();
        const bool rising = curve.getPointValue (dragPoint + 1) >= curve.getPointValue (dragPoint);
        const float delta = (float) (e.y - dragStartY) / 80.0f;
        au::setCurve (*dragParam, dragPoint, dragCurveStart + (rising ? delta : -delta));
        repaint();
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
        arrangement::resizeClip (*selectedClip, end - dragClipStart, te::BeatDuration::fromBeats (gridBeats()));
    }

    repaint();
}

void ArrangementView::mouseUp (const juce::MouseEvent&)
{
    drag = Drag::none;
    dragParam = nullptr;
    repaint();
}

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto l = automationLaneAt (e.getPosition()))
    {
        // Doppio clic su un punto: lo toglie
        if (const int index = automationPointAt (*l, e.getPosition()); index >= 0)
        {
            edit->getUndoManager().beginNewTransaction ("Elimina punto");
            au::removePoint (*l->param, index);
            repaint();
        }

        return;
    }

    if (edit == nullptr || ! lanesArea().contains (e.getPosition()) || clipAt (e.getPosition()) != nullptr)
        return;

    if (auto t = trackAtY (e.y))
    {
        if (freeze::isFrozen (*t))
        {
            if (onStatus)
                onStatus (freezeui::frozenNotice (*t));

            return;
        }

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

    if (auto c = clipAt (e.getPosition()); c != nullptr && ! isOnFrozenTrack (*c))
        newCursor = clipRect (*c).getRight() - e.x <= resizeEdge ? juce::MouseCursor::LeftRightResizeCursor
                                                              : juce::MouseCursor::DraggingHandCursor;

    if (auto l = automationLaneAt (e.getPosition()); l && e.x >= lanesArea().getX())
    {
        newCursor = automationPointAt (*l, e.getPosition()) >= 0 ? juce::MouseCursor::DraggingHandCursor
                                                                 : juce::MouseCursor::CrosshairCursor;
        repaint (l->lane);
    }

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
        int total = 0;

        for (auto t : te::getAudioTracks (*edit))
            total += trackBlockHeight (*t);

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

juce::String ArrangementView::getHintAt (juce::Point<int> pos)
{
    if (edit == nullptr)
        return {};

    static const juce::String zoomHint = juce::String::fromUTF8 ("  \xc2\xb7  Ctrl+rotella zoom, Shift+rotella scorre");

    if (rulerArea().contains (pos))
        return juce::String::fromUTF8 ("Righello: clic per spostare la testina \xc2\xb7 trascina per disegnare il loop");

    if (auto l = automationLaneAt (pos); l && pos.x >= lanesArea().getX())
        return automationPointAt (*l, pos) >= 0
                 ? juce::String::fromUTF8 ("Punto: trascina per spostarlo (Ctrl senza griglia) \xc2\xb7 doppio clic per toglierlo")
                 : juce::String::fromUTF8 ("Corsia di automazione: clic per aggiungere un punto \xc2\xb7 Alt+trascina per curvare un tratto");

    if (pos.x < lanesArea().getX())
    {
        if (auto t = trackAtY (pos.y))
        {
            const int index = te::getAudioTracks (*edit).indexOf (t);

            if (recorder != nullptr && armArea (index).expanded (4).contains (pos))
                return recordingui::armTooltip (*recorder, *t);

            if (automationButtonArea (index).expanded (2).contains (pos))
                return "Corsie di automazione della traccia";

            return juce::String::fromUTF8 ("Traccia: clic per selezionarla \xc2\xb7 clic destro per ingresso, automazioni, congela");
        }

        return {};
    }

    if (auto c = clipAt (pos))
    {
        if (isOnFrozenTrack (*c))
            if (auto t = dynamic_cast<te::AudioTrack*> (c->getTrack()))
                return freezeui::frozenNotice (*t);

        if (clipRect (*c).getRight() - pos.x <= resizeEdge)
            return "Trascina il bordo per allungare o accorciare la clip";

        return juce::String::fromUTF8 ("Clip: trascina per spostarla \xc2\xb7 Ctrl+D duplica \xc2\xb7 Canc elimina \xc2\xb7 clic destro per il menu");
    }

    if (auto t = trackAtY (pos.y))
    {
        if (freeze::isFrozen (*t))
            return freezeui::frozenNotice (*t);

        return juce::String::fromUTF8 ("Doppio clic per una nuova clip MIDI \xc2\xb7 trascina qui un file audio o MIDI") + zoomHint;
    }

    return {};
}

double ArrangementView::gridBeats() const
{
    if (! adaptiveGrid)
        return snapBeats;

    const double bar = edit != nullptr ? arrangement::barLength (*edit).inBeats() : 4.0;
    return arrangement::adaptiveGridBeats (pixelsPerBeat, bar);
}

void ArrangementView::updateSnapButton()
{
    const double bar = edit != nullptr ? arrangement::barLength (*edit).inBeats() : 4.0;
    const auto text = "Griglia: " + (adaptiveGrid ? "auto " + arrangement::gridName (gridBeats(), bar)
                                                   : arrangement::gridName (snapBeats, bar));

    if (snapButton.getButtonText() != text)
        snapButton.setButtonText (text);
}

void ArrangementView::showSnapMenu()
{
    juce::PopupMenu m;
    m.addItem ("Adattiva (segue lo zoom)", true, adaptiveGrid, [this]
    {
        adaptiveGrid = true;
        updateSnapButton();
        repaint();
    });
    m.addSeparator();
    const std::pair<const char*, double> sizes[] = { { "1 battuta", 0.0 }, { "1/2", 2.0 }, { "1/4", 1.0 },
                                                     { "1/8", 0.5 }, { "1/16", 0.25 } };

    for (auto [name, beats] : sizes)
    {
        const double value = beats > 0.0 ? beats : (edit != nullptr ? arrangement::barLength (*edit).inBeats() : 4.0);
        m.addItem (name, true, ! adaptiveGrid && std::abs (snapBeats - value) < 1.0e-6, [this, value]
        {
            snapBeats = value;
            adaptiveGrid = false;
            updateSnapButton();
            repaint();
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
    const auto start = te::BeatPosition::fromBeats (std::floor (juce::jmax (0.0, beatForX (pos.x)) / gridBeats()) * gridBeats());

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
