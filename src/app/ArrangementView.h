/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include "Widgets.h"

namespace sonora::recording { class Recorder; }
namespace sonora::anim { class PlayheadOverlay; }

namespace sonora
{

/** Arrangement: le tracce sulla linea del tempo.

    - Righello: clic per spostare la testina, Shift+trascina per il loop.
    - Doppio clic su una corsia vuota: nuova clip MIDI di una battuta.
    - Trascina una clip per spostarla (anche su un'altra traccia), dal bordo
      destro per allungarla. Ctrl+D duplica, Canc elimina.
    - Trascina file audio o MIDI su una corsia per importarli.
    - Rotella: scorre le tracce. Shift+rotella: scorre il tempo. Ctrl+rotella: zoom.
    - Corsie di automazione sotto le tracce: clic per aggiungere un punto e trascinarlo,
      doppio clic per toglierlo, Alt+trascina un tratto per curvarlo, Ctrl per non
      agganciarsi alla griglia. Il pulsante con la spezzata nell'intestazione sceglie
      quali parametri mostrare.
*/
class ArrangementView final : public juce::Component,
                              public juce::FileDragAndDropTarget,
                              public juce::DragAndDropTarget,
                              private juce::Timer
{
public:
    ArrangementView();
    ~ArrangementView() override;

    void setEdit (tracktion::Edit*);

    /** Armamento delle tracce e registrazione in corso (puo' essere nullptr). */
    void setRecorder (recording::Recorder* r)      { recorder = r; repaint(); }

    std::function<tracktion::AudioTrack*()> getSelectedTrack;
    std::function<void (tracktion::AudioTrack*)> onTrackSelected;
    std::function<void (tracktion::Clip*)> onClipSelected;
    std::function<void (const juce::String&)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

private:
    enum class Drag { none, move, resize, ruler, loop, point, curve };

    /** Una corsia di automazione visibile sotto una traccia. */
    struct AutomationLane
    {
        tracktion::AudioTrack* track = nullptr;
        juce::String key;
        tracktion::AutomatableParameter::Ptr param;   ///< nullptr se il device non c'e' piu'
        juce::Rectangle<int> header, lane;
    };

    void timerCallback() override;
    void showSnapMenu();
    void showClipMenu (tracktion::Clip&);
    void dropFile (const juce::File&, juce::Point<int>);
    void selectClip (tracktion::Clip*);
    void fitToContent();

    juce::Rectangle<int> rulerArea() const;
    juce::Rectangle<int> lanesArea() const;
    int xForBeat (double beat) const;
    double beatForX (int x) const;
    tracktion::AudioTrack* trackAtY (int y) const;
    int laneTop (int index) const;
    juce::Rectangle<int> clipRect (tracktion::Clip&) const;
    tracktion::Clip* clipAt (juce::Point<int>) const;
    tracktion::BeatPosition snap (double beat) const;
    void drawClip (juce::Graphics&, tracktion::Clip&, juce::Rectangle<int>);
    tracktion::SmartThumbnail* getThumbnail (tracktion::WaveAudioClip&);

    // Automazioni
    int trackBlockHeight (const tracktion::AudioTrack&) const;
    std::vector<AutomationLane> automationLanes (tracktion::AudioTrack&, int trackIndex) const;
    std::optional<AutomationLane> automationLaneAt (juce::Point<int>) const;
    int automationPointAt (const AutomationLane&, juce::Point<int>) const;
    int xForTime (tracktion::TimePosition) const;
    tracktion::TimePosition timeForX (int x, bool snapToGrid) const;
    float yForValue (juce::Rectangle<int> lane, float normalised) const;
    float valueForY (juce::Rectangle<int> lane, int y) const;
    juce::Rectangle<int> hideButtonArea (const AutomationLane&) const;
    juce::Rectangle<int> automationButtonArea (int laneIndex) const;
    void drawAutomationLane (juce::Graphics&, const AutomationLane&, juce::Colour, bool selected);
    juce::PopupMenu automationMenu (tracktion::AudioTrack&);
    void showLaneMenu (const AutomationLane&, int pointIndex);

    tracktion::Edit* edit = nullptr;
    tracktion::Clip::Ptr selectedClip;
    std::map<tracktion::EditItemID, std::unique_ptr<tracktion::SmartThumbnail>> thumbnails;

    juce::Rectangle<int> armArea (int laneIndex) const;
    void showHeaderMenu (tracktion::AudioTrack&);

    recording::Recorder* recorder = nullptr;
    std::unique_ptr<anim::PlayheadOverlay> playhead;
    double pixelsPerBeat = 24.0, viewStart = 0.0;
    bool needsFit = true; // l'adattamento aspetta di conoscere la larghezza vera
    int scrollY = 0, laneHeight = 64;
    double snapBeats = 1.0;

    Drag drag = Drag::none;
    tracktion::BeatPosition dragClipStart;
    tracktion::BeatDuration dragClipLength;
    double dragStartBeat = 0.0;

    tracktion::AutomatableParameter::Ptr dragParam;
    juce::Rectangle<int> dragLane;
    int dragPoint = -1, dragStartY = 0;
    float dragCurveStart = 0.0f;

    FlatButton backButton { "Torna all'arrangement", 12.0f }, snapButton { "Griglia: 1/4", 12.0f },
               fitButton { "Adatta", 12.0f };
};

} // namespace sonora
