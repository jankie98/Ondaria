/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Automation.h"

namespace te = tracktion;

namespace sonora::automation
{

namespace
{
    const juce::Identifier lanesProperty ("sonoraAutomationLanes");

    bool isMeter (te::Plugin* p)
    {
        return dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr;
    }

    juce::UndoManager* undo (te::AutomatableParameter& p)
    {
        return &p.getEdit().getUndoManager();
    }
}

juce::String keyFor (te::AutomatableParameter& p)
{
    auto plugin = p.getPlugin();
    return (plugin != nullptr ? plugin->itemID.toString() : juce::String()) + "/" + p.paramID;
}

juce::String displayName (te::AutomatableParameter& p)
{
    auto plugin = p.getPlugin();
    const auto owner = dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr ? juce::String ("Mixer")
                                                                                   : (plugin != nullptr ? plugin->getName() : juce::String());
    return owner.isNotEmpty() ? owner + ": " + p.getParameterName() : p.getParameterName();
}

std::vector<ParameterInfo> getParameters (te::AudioTrack& t)
{
    std::vector<ParameterInfo> result;

    auto add = [&result] (te::AutomatableParameter* p)
    {
        if (p != nullptr)
            result.push_back ({ p, keyFor (*p), displayName (*p) });
    };

    if (auto vp = t.getVolumePlugin())
    {
        add (vp->volParam.get());
        add (vp->panParam.get());
    }

    for (auto plugin : t.pluginList.getPlugins())
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (plugin) != nullptr || isMeter (plugin))
            continue;

        for (auto p : plugin->getAutomatableParameters())
            add (p);
    }

    return result;
}

te::AutomatableParameter::Ptr findParameter (te::AudioTrack& t, const juce::String& key)
{
    for (auto& info : getParameters (t))
        if (info.key == key)
            return info.param;

    return {};
}

//==============================================================================
juce::StringArray getShownLanes (const te::AudioTrack& t)
{
    return juce::StringArray::fromTokens (t.state.getProperty (lanesProperty).toString(), "|", {});
}

bool isLaneShown (const te::AudioTrack& t, const juce::String& key)
{
    return getShownLanes (t).contains (key);
}

void setLaneShown (te::AudioTrack& t, const juce::String& key, bool shown)
{
    auto lanes = getShownLanes (t);

    if (shown == lanes.contains (key))
        return;

    if (shown)
        lanes.add (key);
    else
        lanes.removeString (key);

    // E' solo vista: non entra nell'annulla
    t.state.setProperty (lanesProperty, lanes.joinIntoString ("|"), nullptr);
}

bool hasAutomation (te::AutomatableParameter& p)
{
    return p.getCurve().getNumPoints() > 0;
}

bool hasAutomation (te::AudioTrack& t)
{
    for (auto& info : getParameters (t))
        if (hasAutomation (*info.param))
            return true;

    return false;
}

int showAutomatedLanes (te::AudioTrack& t)
{
    int opened = 0;

    for (auto& info : getParameters (t))
    {
        if (hasAutomation (*info.param) && ! isLaneShown (t, info.key))
        {
            setLaneShown (t, info.key, true);
            ++opened;
        }
    }

    return opened;
}

void hideAllLanes (te::AudioTrack& t)
{
    t.state.removeProperty (lanesProperty, nullptr);
}

//==============================================================================
float toNormalised (const te::AutomatableParameter& p, float value)
{
    return juce::jlimit (0.0f, 1.0f, p.valueRange.convertTo0to1 (juce::jlimit (p.valueRange.start, p.valueRange.end, value)));
}

float fromNormalised (const te::AutomatableParameter& p, float normalised)
{
    return p.valueRange.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised));
}

juce::String valueText (te::AutomatableParameter& p, float value)
{
    auto text = p.valueToString (value);
    const auto label = p.getLabel();
    return label.isNotEmpty() && ! text.endsWith (label) ? text + " " + label : text;
}

int addPoint (te::AutomatableParameter& p, te::TimePosition t, float normalised)
{
    return p.getCurve().addPoint (juce::jmax (te::TimePosition(), t), fromNormalised (p, normalised), 0.0f, undo (p));
}

int movePoint (te::AutomatableParameter& p, int index, te::TimePosition t, float normalised)
{
    return p.getCurve().movePoint (p, index, juce::jmax (te::TimePosition(), t), fromNormalised (p, normalised), false, undo (p));
}

void removePoint (te::AutomatableParameter& p, int index)
{
    if (index >= 0 && index < p.getCurve().getNumPoints())
        p.getCurve().removePoint (index, undo (p));
}

void clear (te::AutomatableParameter& p)
{
    p.getCurve().clear (undo (p));
}

void setCurve (te::AutomatableParameter& p, int index, float curve)
{
    if (index >= 0 && index < p.getCurve().getNumPoints())
        p.getCurve().setCurveValue (index, juce::jlimit (-1.0f, 1.0f, curve), undo (p));
}

float normalisedValueAt (te::AutomatableParameter& p, te::TimePosition t)
{
    auto& curve = p.getCurve();

    if (curve.getNumPoints() == 0)
        return toNormalised (p, p.getCurrentBaseValue());

    return toNormalised (p, curve.getValueAt (t, p.getCurrentBaseValue()));
}

//==============================================================================
bool isWriting (te::Edit& e)
{
    return e.getAutomationRecordManager().isWritingAutomation();
}

void setWriting (te::Edit& e, bool shouldWrite)
{
    auto& rm = e.getAutomationRecordManager();

    if (! shouldWrite && rm.isRecordingAutomation())
        rm.punchOut (false);

    // Si scrive finche' si tiene la manopola (il motore di serie non scriverebbe nulla)
    if (shouldWrite)
        for (auto t : te::getAudioTracks (e))
            if (t->automationMode.get() == te::AutomationMode::read)
                t->automationMode.setValue (te::AutomationMode::touch, nullptr);

    rm.setWritingAutomation (shouldWrite);
}

void prepareEdit (te::Edit& e)
{
    // Per non scrivere punti per sbaglio: si scrive solo col pulsante acceso
    e.getAutomationRecordManager().setWritingAutomation (false);
    e.getAutomationRecordManager().setReadingAutomation (true);
}

bool showLanesBeingWritten (te::Edit& e)
{
    auto& rm = e.getAutomationRecordManager();

    if (! rm.isRecordingAutomation())
        return false;

    bool opened = false;

    for (auto t : te::getAudioTracks (e))
    {
        for (auto& info : getParameters (*t))
        {
            if (rm.isParameterRecording (info.param.get()) && ! isLaneShown (*t, info.key))
            {
                setLaneShown (*t, info.key, true);
                opened = true;
            }
        }
    }

    return opened;
}

} // namespace sonora::automation
