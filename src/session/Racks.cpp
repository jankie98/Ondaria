/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "Racks.h"

namespace te = tracktion;

namespace sonora::racks
{

namespace
{
    const juce::Identifier chainsID ("SONORACHAINS"), chainID ("CHAIN"),
                           nameID ("name"), devicesID ("devices"), faderID ("fader"),
                           mutedID ("muted"), soloedID ("soloed"), selectedID ("selected");

    constexpr int maxAudioPins = 2;

    juce::ValueTree chainsTree (const te::RackType& rack)
    {
        return rack.state.getChildWithName (chainsID);
    }

    juce::ValueTree chainTree (const te::RackType& rack, int chain)
    {
        return chainsTree (rack).getChild (chain);
    }

    juce::UndoManager* um (te::RackType& rack)     { return &rack.edit.getUndoManager(); }

    std::vector<te::EditItemID> parseIDs (const juce::String& s)
    {
        std::vector<te::EditItemID> ids;

        for (auto& token : juce::StringArray::fromTokens (s, ",", {}))
            if (auto id = te::EditItemID::fromString (token.trim()); id.isValid())
                ids.push_back (id);

        return ids;
    }

    juce::String joinIDs (const std::vector<te::EditItemID>& ids)
    {
        juce::StringArray s;

        for (auto& id : ids)
            s.add (id.toString());

        return s.joinIntoString (",");
    }

    std::vector<te::EditItemID> getDeviceIDs (const te::RackType& rack, int chain)
    {
        return parseIDs (chainTree (rack, chain)[devicesID].toString());
    }

    void setDeviceIDs (te::RackType& rack, int chain, const std::vector<te::EditItemID>& ids)
    {
        chainTree (rack, chain).setProperty (devicesID, joinIDs (ids), um (rack));
    }

    int numAudio (te::Plugin& p, bool inputs)
    {
        juce::StringArray ins, outs;
        p.getChannelNames (&ins, &outs);
        return juce::jmin (maxAudioPins, (inputs ? ins : outs).size());
    }

    /** Collega l'audio da src a dst (EditItemID vuoto: ingresso o uscita del rack). Da
        mono a stereo duplica il canale, da stereo a mono somma. */
    void connectAudio (te::RackType& rack, te::Plugin* src, te::Plugin* dst)
    {
        const int outs = src != nullptr ? numAudio (*src, false) : maxAudioPins;
        const int ins = dst != nullptr ? numAudio (*dst, true) : maxAudioPins;

        if (outs == 0 || ins == 0)
            return;

        const auto srcID = src != nullptr ? src->itemID : te::EditItemID();
        const auto dstID = dst != nullptr ? dst->itemID : te::EditItemID();

        for (int i = 0; i < juce::jmax (outs, ins); ++i)
            rack.addConnection (srcID, juce::jmin (i, outs - 1) + 1, dstID, juce::jmin (i, ins - 1) + 1);
    }

    juce::Point<float> positionFor (int chain, int index)
    {
        // Solo per la finestra del rack di Tracktion: le catene in righe, i device in colonna.
        return { juce::jlimit (0.05f, 0.95f, 0.1f + 0.12f * (float) index),
                 juce::jlimit (0.05f, 0.95f, 0.15f + 0.15f * (float) chain) };
    }

    te::VolumeAndPanPlugin::Ptr createFader (te::RackType& rack, int chain)
    {
        auto p = rack.edit.getPluginCache().createNewPlugin (te::VolumeAndPanPlugin::xmlTypeName, {});
        auto fader = dynamic_cast<te::VolumeAndPanPlugin*> (p.get());

        if (fader == nullptr || ! rack.addPlugin (p, positionFor (chain, 9), false))
            return {};

        return fader;
    }

    void ensureModel (te::RackType& rack)
    {
        if (! chainsTree (rack).isValid())
            rack.state.addChild (juce::ValueTree (chainsID), -1, um (rack));
    }

    juce::String defaultChainName (const te::RackType& rack)
    {
        juce::StringArray used;

        for (auto c : chainsTree (rack))
            used.add (c[nameID].toString());

        for (int i = 1;; ++i)
            if (auto name = "Catena " + juce::String (i); ! used.contains (name))
                return name;
    }

    bool anySolo (const te::RackType& rack)
    {
        for (auto c : chainsTree (rack))
            if ((bool) c[soloedID])
                return true;

        return false;
    }

    te::RackType::Ptr typeFor (const te::Plugin& p)
    {
        if (auto ri = dynamic_cast<const te::RackInstance*> (&p))
            return ri->type;

        return {};
    }

    /** Le assegnazioni delle macro di questo rack su un parametro. */
    te::MacroParameter::Assignment* findAssignment (te::AutomatableParameter& param, const te::MacroParameter& macro)
    {
        for (auto a : param.getAssignments())
            if (auto ma = dynamic_cast<te::MacroParameter::Assignment*> (a))
                if (ma->isForModifierSource (macro))
                    return ma;

        return nullptr;
    }
}

//==============================================================================
bool isRack (const te::Plugin& p)
{
    return getRackType (p) != nullptr;
}

te::RackType* getRackType (const te::Plugin& p)
{
    if (auto type = typeFor (p); type != nullptr && chainsTree (*type).isValid())
        return type.get();

    return nullptr;
}

te::RackType* getContainingRack (const te::Plugin& p)
{
    if (! p.isInRack())
        return nullptr;

    if (auto type = p.getOwnerRackType(); type != nullptr && chainsTree (*type).isValid())
        return type.get();

    return nullptr;
}

te::Plugin* findRackInstance (te::Track& track, const te::RackType& rack)
{
    for (auto p : track.pluginList.getPlugins())
        if (getRackType (*p) == &rack)
            return p;

    return nullptr;
}

//==============================================================================
te::Plugin::Ptr createRack (te::AudioTrack& track, int index, const juce::String& name)
{
    auto& edit = track.edit;
    auto rack = edit.getRackList().addNewRack();

    if (rack == nullptr)
        return {};

    rack->rackName = name.isNotEmpty() ? name : juce::String ("Rack");
    ensureModel (*rack);
    getMacros (*rack);
    addChain (*rack);

    return track.pluginList.insertPlugin (te::RackInstance::create (*rack), index);
}

te::Plugin::Ptr group (te::AudioTrack& track, const te::Plugin::Array& plugins)
{
    // Nell'ordine della traccia, solo quelli che ci sono davvero
    te::Plugin::Array ordered;
    int firstIndex = -1;

    for (int i = 0; i < track.pluginList.size(); ++i)
        if (auto p = track.pluginList[i]; p != nullptr && plugins.contains (p))
        {
            if (firstIndex < 0)
                firstIndex = i;

            ordered.add (p);
        }

    if (ordered.isEmpty())
        return {};

    auto name = ordered.size() == 1 ? ordered.getFirst()->getName() : juce::String ("Rack");
    auto instance = createRack (track, firstIndex, name);

    if (instance == nullptr)
        return {};

    auto& rack = *getRackType (*instance);

    for (auto p : ordered)
        insertPlugin (rack, 0, p);

    return instance;
}

bool canUngroup (const te::Plugin& p)
{
    auto rack = getRackType (p);
    return rack != nullptr && getNumChains (*rack) == 1;
}

bool ungroup (te::Plugin& instance)
{
    if (! canUngroup (instance))
        return false;

    auto track = dynamic_cast<te::AudioTrack*> (instance.getOwnerTrack());

    if (track == nullptr)
        return false;

    te::RackType::Ptr rack (getRackType (instance));
    int index = track->pluginList.indexOf (&instance);

    // Le macro restano col rack: prima togliamo le loro assegnazioni dai device
    for (int m = 0; m < numMacros; ++m)
        for (auto& mapping : getMappings (*rack, m))
            unmap (*rack, m, *mapping.parameter);

    for (auto p : getChainPlugins (*rack, 0))
        track->pluginList.insertPlugin (p, index++, nullptr);

    setDeviceIDs (*rack, 0, {});
    rewire (*rack);
    instance.deleteFromParent();

    // Il tipo di rack, ormai vuoto, resta nell'Edit finche' c'e' l'undo: toglierlo e
    // poi rimetterlo con l'undo lascerebbe in giro il vecchio oggetto con lo stesso id.
    // Lo pulisce removeUnusedRacks quando si riapre il progetto.
    return true;
}

void deleteRack (te::Plugin& instance)
{
    // Come per ungroup, il tipo con i suoi device resta fino alla prossima apertura
    instance.deleteFromParent();
}

void removeUnusedRacks (juce::ValueTree& editState)
{
    juce::StringArray used;

    std::function<void (const juce::ValueTree&)> collect = [&] (const juce::ValueTree& v)
    {
        if (v.hasType (te::IDs::PLUGIN) && v[te::IDs::type].toString() == te::RackInstance::xmlTypeName)
            used.addIfNotAlreadyThere (v[te::IDs::rackType].toString());

        for (auto child : v)
            collect (child);
    };

    collect (editState);

    auto list = editState.getChildWithName (te::IDs::RACKS);

    for (int i = list.getNumChildren(); --i >= 0;)
        if (auto rack = list.getChild (i); rack.hasType (te::IDs::RACK) && ! used.contains (rack[te::IDs::id].toString()))
            list.removeChild (i, nullptr);
}

//==============================================================================
int getNumChains (const te::RackType& rack)
{
    return chainsTree (rack).getNumChildren();
}

int addChain (te::RackType& rack, const juce::String& name)
{
    ensureModel (rack);
    const int index = getNumChains (rack);
    auto fader = createFader (rack, index);

    juce::ValueTree c (chainID);
    c.setProperty (nameID, name.isNotEmpty() ? name : defaultChainName (rack), nullptr);
    c.setProperty (faderID, fader != nullptr ? fader->itemID.toString() : juce::String(), nullptr);
    chainsTree (rack).addChild (c, -1, um (rack));

    setSelectedChain (rack, index);
    rewire (rack);
    return index;
}

void removeChain (te::RackType& rack, int chain)
{
    if (getNumChains (rack) <= 1 || ! chainTree (rack, chain).isValid())
        return;

    for (auto p : getChainPlugins (rack, chain))
        p->deleteFromParent();

    if (auto fader = getChainFader (rack, chain))
        fader->deleteFromParent();

    chainsTree (rack).removeChild (chain, um (rack));
    setSelectedChain (rack, juce::jmin (chain, getNumChains (rack) - 1));
    rewire (rack);
}

int duplicateChain (te::RackType& rack, int chain)
{
    auto source = chainTree (rack, chain);

    if (! source.isValid())
        return -1;

    auto& edit = rack.edit;

    auto copyPlugin = [&] (te::Plugin& p, int position) -> te::Plugin::Ptr
    {
        p.flushPluginStateToValueTree();
        auto state = p.state.createCopy();
        te::EditItemID::remapIDs (state, nullptr, edit);

        if (auto copy = edit.getPluginCache().getOrCreatePluginFor (state))
            if (rack.addPlugin (copy, positionFor (chain + 1, position), false))
                return copy;

        return {};
    };

    std::vector<te::EditItemID> devices;
    int position = 0;

    for (auto p : getChainPlugins (rack, chain))
        if (auto copy = copyPlugin (*p, position++))
            devices.push_back (copy->itemID);

    te::Plugin::Ptr fader;

    if (auto f = getChainFader (rack, chain))
        fader = copyPlugin (*f, 9);

    juce::ValueTree c (chainID);
    c.copyPropertiesFrom (source, nullptr);
    c.setProperty (nameID, source[nameID].toString() + " (copia)", nullptr);
    c.setProperty (devicesID, joinIDs (devices), nullptr);
    c.setProperty (faderID, fader != nullptr ? fader->itemID.toString() : juce::String(), nullptr);
    chainsTree (rack).addChild (c, chain + 1, um (rack));

    setSelectedChain (rack, chain + 1);
    rewire (rack);
    return chain + 1;
}

juce::String getChainName (const te::RackType& rack, int chain)
{
    return chainTree (rack, chain)[nameID].toString();
}

void setChainName (te::RackType& rack, int chain, const juce::String& name)
{
    if (auto c = chainTree (rack, chain); c.isValid() && name.trim().isNotEmpty())
        c.setProperty (nameID, name.trim(), um (rack));
}

te::Plugin::Array getChainPlugins (te::RackType& rack, int chain)
{
    te::Plugin::Array result;

    for (auto& id : getDeviceIDs (rack, chain))
        if (auto p = rack.getPluginForID (id))
            result.add (p);

    return result;
}

te::VolumeAndPanPlugin* getChainFader (te::RackType& rack, int chain)
{
    auto id = te::EditItemID::fromString (chainTree (rack, chain)[faderID].toString());
    return id.isValid() ? dynamic_cast<te::VolumeAndPanPlugin*> (rack.getPluginForID (id)) : nullptr;
}

int findChainOf (te::RackType& rack, const te::Plugin& p)
{
    for (int i = 0; i < getNumChains (rack); ++i)
    {
        auto ids = getDeviceIDs (rack, i);

        if (std::find (ids.begin(), ids.end(), p.itemID) != ids.end())
            return i;
    }

    return -1;
}

bool isChainMuted (const te::RackType& rack, int chain)       { return (bool) chainTree (rack, chain)[mutedID]; }
bool isChainSoloed (const te::RackType& rack, int chain)      { return (bool) chainTree (rack, chain)[soloedID]; }

void setChainMuted (te::RackType& rack, int chain, bool b)
{
    if (auto c = chainTree (rack, chain); c.isValid() && (bool) c[mutedID] != b)
    {
        c.setProperty (mutedID, b, um (rack));
        rewire (rack);
    }
}

void setChainSoloed (te::RackType& rack, int chain, bool b)
{
    if (auto c = chainTree (rack, chain); c.isValid() && (bool) c[soloedID] != b)
    {
        c.setProperty (soloedID, b, um (rack));
        rewire (rack);
    }
}

bool isChainAudible (const te::RackType& rack, int chain)
{
    return ! isChainMuted (rack, chain) && (! anySolo (rack) || isChainSoloed (rack, chain));
}

int getSelectedChain (const te::RackType& rack)
{
    return juce::jlimit (0, juce::jmax (0, getNumChains (rack) - 1), (int) chainsTree (rack)[selectedID]);
}

void setSelectedChain (te::RackType& rack, int chain)
{
    if (auto t = chainsTree (rack); t.isValid())
        t.setProperty (selectedID, chain, nullptr);
}

bool insertPlugin (te::RackType& rack, int chain, const te::Plugin::Ptr& p, int position)
{
    if (p == nullptr || ! chainTree (rack, chain).isValid() || ! te::RackType::isPluginAllowed (p))
        return false;

    // Da un'altra catena di questo rack: prima lo togliamo dalla lista di quella
    if (auto from = getContainingRack (*p))
    {
        if (auto old = findChainOf (*from, *p); old >= 0)
        {
            auto ids = getDeviceIDs (*from, old);
            ids.erase (std::remove (ids.begin(), ids.end(), p->itemID), ids.end());
            setDeviceIDs (*from, old, ids);
        }

        if (from != &rack)
        {
            p->removeFromParent();
            rewire (*from);
        }
    }

    auto ids = getDeviceIDs (rack, chain);

    if (rack.getPluginForID (p->itemID) == nullptr
         && ! rack.addPlugin (p, positionFor (chain, (int) ids.size()), false))
        return false;

    if (position < 0 || position > (int) ids.size())
        position = (int) ids.size();

    ids.insert (ids.begin() + position, p->itemID);
    setDeviceIDs (rack, chain, ids);
    rewire (rack);
    return true;
}

void removePlugin (te::RackType& rack, te::Plugin& p)
{
    te::Plugin::Ptr keepAlive (&p);

    if (auto chain = findChainOf (rack, p); chain >= 0)
    {
        auto ids = getDeviceIDs (rack, chain);
        ids.erase (std::remove (ids.begin(), ids.end(), p.itemID), ids.end());
        setDeviceIDs (rack, chain, ids);
    }

    p.deleteFromParent();
    rewire (rack);
}

void rewire (te::RackType& rack)
{
    // Tutti i collegamenti si rifanno da capo: sono pochi e cosi' non si sbaglia.
    juce::Array<std::tuple<te::EditItemID, int, te::EditItemID, int>> old;

    for (auto c : rack.getConnections())
        old.add ({ c->sourceID.get(), c->sourcePin.get(), c->destID.get(), c->destPin.get() });

    for (auto& [s, sp, d, dp] : old)
        rack.removeConnection (s, sp, d, dp);

    // Il MIDI attraversa il rack, per i device che vengono dopo sulla traccia
    rack.addConnection ({}, 0, {}, 0);

    for (int chain = 0; chain < getNumChains (rack); ++chain)
    {
        // I device spariti (eliminati da fuori) escono dalla lista
        auto plugins = getChainPlugins (rack, chain);

        if ((int) getDeviceIDs (rack, chain).size() != plugins.size())
        {
            std::vector<te::EditItemID> ids;

            for (auto p : plugins)
                ids.push_back (p->itemID);

            setDeviceIDs (rack, chain, ids);
        }

        te::Plugin::Ptr fader (getChainFader (rack, chain));

        if (fader == nullptr)
        {
            fader = createFader (rack, chain);

            if (fader != nullptr)
                chainTree (rack, chain).setProperty (faderID, fader->itemID.toString(), um (rack));
        }

        te::Plugin* previous = nullptr;   // ingresso del rack

        for (auto p : plugins)
        {
            rack.addConnection ({}, 0, p->itemID, 0);
            connectAudio (rack, previous, p);

            // Uno strumento non ha ingressi audio: il segnale riparte da lui
            previous = p;
        }

        if (fader != nullptr)
        {
            connectAudio (rack, previous, fader.get());

            if (isChainAudible (rack, chain))
                connectAudio (rack, fader.get(), nullptr);
        }
    }
}

//==============================================================================
juce::ReferenceCountedArray<te::MacroParameter> getMacros (te::RackType& rack)
{
    auto& list = rack.getMacroParameterListForWriting();
    auto macros = list.getMacroParameters();

    while (macros.size() < numMacros)
    {
        if (auto m = list.createMacroParameter())
        {
            m->macroName = "Macro " + juce::String (macros.size() + 1);
            m->value = 0.0f;
            m->setParameter (0.0f, juce::dontSendNotification);
        }

        auto updated = list.getMacroParameters();

        if (updated.size() == macros.size())
            break;

        macros = updated;
    }

    return macros;
}

te::MacroParameter* getMacro (te::RackType& rack, int index)
{
    auto macros = getMacros (rack);
    return juce::isPositiveAndBelow (index, macros.size()) ? macros[index].get() : nullptr;
}

juce::String getMacroName (te::RackType& rack, int index)
{
    if (auto m = getMacro (rack, index))
        if (auto n = m->macroName.get(); n.isNotEmpty())
            return n;

    return "Macro " + juce::String (index + 1);
}

void setMacroName (te::RackType& rack, int index, const juce::String& name)
{
    if (auto m = getMacro (rack, index); m != nullptr && name.trim().isNotEmpty())
        m->macroName = name.trim();
}

float getMacroValue (te::RackType& rack, int index)
{
    if (auto m = getMacro (rack, index))
        return m->getCurrentValue();

    return 0.0f;
}

void setMacroValue (te::RackType& rack, int index, float v)
{
    if (auto m = getMacro (rack, index))
        m->setParameter (juce::jlimit (0.0f, 1.0f, v), juce::sendNotification);
}

std::vector<Mapping> getMappings (te::RackType& rack, int macro)
{
    std::vector<Mapping> result;
    auto m = getMacro (rack, macro);

    if (m == nullptr)
        return result;

    for (auto p : rack.getPlugins())
        for (auto param : p->getAutomatableParameters())
            if (auto a = findAssignment (*param, *m))
            {
                const auto minimum = param->valueRange.convertTo0to1 (param->getCurrentBaseValue());
                result.push_back ({ param, minimum, minimum + a->value.get() });
            }

    return result;
}

bool map (te::RackType& rack, int macro, te::AutomatableParameter& param)
{
    auto m = getMacro (rack, macro);
    auto owner = dynamic_cast<te::Plugin*> (&param.automatableEditElement);

    if (m == nullptr || owner == nullptr || getContainingRack (*owner) != &rack)
        return false;

    for (int other = 0; other < numMacros; ++other)
        if (other != macro)
            unmap (rack, other, param);

    if (findAssignment (param, *m) == nullptr)
        param.addModifier (*m, 1.0f, 0.0f, 0.0f);

    setMappingRange (rack, macro, param, 0.0f, 1.0f);
    return true;
}

void setMappingRange (te::RackType& rack, int macro, te::AutomatableParameter& param, float minimum, float maximum)
{
    auto m = getMacro (rack, macro);

    if (m == nullptr)
        return;

    if (auto a = findAssignment (param, *m))
    {
        minimum = juce::jlimit (0.0f, 1.0f, minimum);
        maximum = juce::jlimit (0.0f, 1.0f, maximum);

        // La base del parametro e' il valore con la macro a 0; la macro aggiunge il resto.
        param.setNormalisedParameter (minimum, juce::sendNotification);
        a->value = maximum - minimum;
        param.updateFromAutomationSources (rack.edit.getTransport().getPosition());
    }
}

void unmap (te::RackType& rack, int macro, te::AutomatableParameter& param)
{
    if (auto m = getMacro (rack, macro))
        if (auto a = findAssignment (param, *m))
        {
            // Il parametro resta dove la macro lo stava portando
            const auto current = param.getCurrentNormalisedValue();
            param.removeModifier (*a);
            param.setNormalisedParameter (current, juce::sendNotification);
        }
}

int getMacroFor (te::RackType& rack, const te::AutomatableParameter& param)
{
    auto macros = getMacros (rack);

    for (int i = 0; i < macros.size(); ++i)
        if (findAssignment (const_cast<te::AutomatableParameter&> (param), *macros[i]) != nullptr)
            return i;

    return -1;
}

} // namespace sonora::racks
