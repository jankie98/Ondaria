/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "DeviceChainComponent.h"
#include "DeviceVisualiser.h"
#include "FreezeUI.h"
#include "PluginIsolation.h"
#include "Settings.h"
#include "Widgets.h"
#include "session/Racks.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    constexpr int boxWidth = 244;
    constexpr int knobsPerRow = 4;
    constexpr int frozenBannerHeight = 34;
    constexpr int rackBoxWidth = 468;
    constexpr int chainListWidth = 226;
    constexpr int chainRowHeight = 24;
    constexpr int emptyChainWidth = 210;
    constexpr int gap = 10;

    bool isMixerPlugin (te::Plugin* p)
    {
        return dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr
            || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr
            || dynamic_cast<te::FreezePointPlugin*> (p) != nullptr;
    }

    /** Le schede si aggiornano dal timer del pannello. */
    struct Refreshable
    {
        virtual ~Refreshable() = default;
        virtual void refresh() = 0;
    };

    juce::String parameterLabel (te::AutomatableParameter& p)
    {
        return p.automatableEditElement.getName() + ": " + p.getParameterName();
    }

    juce::String percent (float v)
    {
        return juce::String (juce::roundToInt (v * 100.0f)) + "%";
    }

    void undoable (te::Edit& edit, const juce::String& name, std::function<void()> f)
    {
        edit.getUndoManager().beginNewTransaction (name);
        f();
    }
}

//==============================================================================
/** Manopola rotante: il clic destro apre un menu invece di muoverla, e si puo'
    bloccare (parametro mosso da una macro). */
class KnobSlider final : public juce::Slider
{
public:
    KnobSlider()
    {
        setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        setRange (0.0, 1.0);
        setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        setPopupDisplayEnabled (false, false, nullptr);
    }

    std::function<void()> onPopupMenu;
    std::function<bool()> isLocked;

    void mouseDown (const juce::MouseEvent& e) override
    {
        ignoreDrag = e.mods.isPopupMenu() || (isLocked != nullptr && isLocked());

        if (e.mods.isPopupMenu())
        {
            if (onPopupMenu != nullptr)
                onPopupMenu();

            return;
        }

        if (! ignoreDrag)
            juce::Slider::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override     { if (! ignoreDrag) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override       { if (! ignoreDrag) juce::Slider::mouseUp (e); ignoreDrag = false; }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! ignoreDrag) juce::Slider::mouseDoubleClick (e); }

private:
    bool ignoreDrag = false;
};

//==============================================================================
/** Una manopola collegata a un parametro automatizzabile del device. Dentro un rack,
    col clic destro si assegna a una macro. */
class ParameterKnob final : public juce::Component
{
public:
    ParameterKnob (te::AutomatableParameter::Ptr p, te::RackType::Ptr r)
        : param (std::move (p)), rack (std::move (r))
    {
        slider.onDragStart = [this] { param->parameterChangeGestureBegin(); };
        slider.onDragEnd = [this] { param->parameterChangeGestureEnd(); };
        slider.onValueChange = [this] { param->setNormalisedParameter ((float) slider.getValue(), juce::sendNotification); };
        slider.isLocked = [this] { return macro >= 0; };
        slider.onPopupMenu = [this] { showMenu(); };
        addAndMakeVisible (slider);
        refresh();
    }

    void setColour (juce::Colour c)     { colour = c; slider.setColour (juce::Slider::rotarySliderFillColourId, c); }

    void refresh()
    {
        if (! slider.isMouseButtonDown())
            slider.setValue (param->getCurrentNormalisedValue(), juce::dontSendNotification);

        auto text = param->getCurrentValueAsStringWithLabel();
        const int newMacro = rack != nullptr ? racks::getMacroFor (*rack, *param) : -1;

        if (text != valueText || newMacro != macro)
        {
            valueText = text;
            macro = newMacro;
            slider.setTooltip (macro >= 0 ? param->getParameterName() + ": la muove " + racks::getMacroName (*rack, macro)
                                                + " (clic destro per toglierla)"
                                          : rack != nullptr ? param->getParameterName() + " (clic destro: assegna a una macro)"
                                                            : param->getParameterName());
            slider.setColour (juce::Slider::rotarySliderFillColourId, macro >= 0 ? colours::accent : colour);
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (macro >= 0)
        {
            // Etichetta della macro sopra la manopola
            auto badge = juce::Rectangle<float> ((float) getWidth() - 24.0f, 0.0f, 22.0f, 14.0f);
            g.setColour (colours::accent.withAlpha (0.2f));
            g.fillRoundedRectangle (badge, 3.0f);
            g.setColour (colours::accent);
            g.setFont (fonts::mono (9.5f));
            g.drawText ("M" + juce::String (macro + 1), badge, juce::Justification::centred);
        }

        auto r = getLocalBounds().withTrimmedTop (40);
        g.setColour (macro >= 0 ? colours::accent : colours::textDim);
        g.setFont (fonts::ui (10.5f));
        g.drawText (param->getParameterShortName (10), r.removeFromTop (14), juce::Justification::centred, true);
        g.setColour (colours::textMid);
        g.setFont (fonts::mono (10.0f));
        g.drawText (valueText, r.removeFromTop (14), juce::Justification::centred, true);
    }

    void resized() override
    {
        slider.setBounds (getLocalBounds().removeFromTop (40).withSizeKeepingCentre (40, 40));
    }

private:
    void showMenu()
    {
        if (rack == nullptr)
            return;

        juce::PopupMenu m, assign;
        auto r = rack;
        auto p = param;

        m.addSectionHeader (param->getParameterName());

        for (int i = 0; i < racks::numMacros; ++i)
            assign.addItem (racks::getMacroName (*r, i), true, i == macro, [r, p, i]
            {
                undoable (r->edit, "Assegna alla macro", [&] { racks::map (*r, i, *p); });
            });

        m.addSubMenu ("Assegna alla macro", assign);

        if (macro >= 0)
            m.addItem ("Togli da " + racks::getMacroName (*r, macro), [r, p, i = macro]
            {
                undoable (r->edit, "Togli dalla macro", [&] { racks::unmap (*r, i, *p); });
            });

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
    }

    te::AutomatableParameter::Ptr param;
    te::RackType::Ptr rack;
    KnobSlider slider;
    juce::String valueText;
    juce::Colour colour { colours::accent };
    int macro = -2;
};

//==============================================================================
class DeviceChainComponent::DeviceBox final : public juce::Component,
                                              public juce::SettableTooltipClient,
                                              public Refreshable
{
public:
    DeviceBox (te::Plugin::Ptr p, juce::Colour c, te::RackType::Ptr containingRack)
        : plugin (std::move (p)), rack (std::move (containingRack)), colour (c)
    {
        powerButton.setActiveColour (colour);
        powerButton.setIconColour (colours::textDim);
        powerButton.onClick = [this]
        {
            undoable (plugin->edit, "Attiva/disattiva device", [this] { plugin->setEnabled (! plugin->isEnabled()); });
            refresh();
        };

        openButton.setBaseColour (colours::panelLight);
        openButton.onClick = [this] { plugin->showWindowExplicitly(); };

        removeButton.setIconColour (colours::textDim);
        removeButton.onClick = [this]
        {
            juce::MessageManager::callAsync ([p = plugin, r = rack]
            {
                undoable (p->edit, "Rimuovi device", [&]
                {
                    if (r != nullptr)
                        racks::removePlugin (*r, *p);
                    else
                        p->deleteFromParent();
                });
            });
        };

        for (auto* b : std::initializer_list<juce::Component*> { &powerButton, &openButton, &removeButton })
            addAndMakeVisible (b);

        if (auto external = dynamic_cast<te::ExternalPlugin*> (plugin.get()))
        {
            format = external->desc.pluginFormatName;

            if (external->getAudioPluginInstance() == nullptr)
                error = "Non caricato: " + external->getLoadError();
            else if (isolation::hasCrashed (*plugin))
                error = juce::String::fromUTF8 ("Si \xc3\xa8 fermato nel suo processo e ora tace. Il progetto non ne risente: "
                                                "riavvialo dal menu (tasto destro).");

            isolated = isolation::isIsolated (*plugin);

            if (isolated)
                setTooltip ("Gira in un processo separato: se va in crash, Ondaria continua a suonare");
        }
        else
        {
            format = "Integrato";
        }

        if ((visualiser = DeviceVisualiser::create (*plugin, colour)))
            addAndMakeVisible (*visualiser);

        // Le prime otto manopole: per tutti i parametri c'e' la finestra del device.
        auto params = plugin->getAutomatableParameters();

        for (int i = 0; i < juce::jmin (8, params.size()); ++i)
        {
            auto* k = knobs.add (new ParameterKnob (params[i], rack));
            k->setColour (colour);
            addAndMakeVisible (k);
        }

        refresh();
    }

    std::function<void (const juce::ModifierKeys&)> onClick;
    std::function<void()> onMenu;

    void setSelected (bool s)
    {
        if (selected != s)
        {
            selected = s;
            repaint();
        }
    }

    void refresh() override
    {
        powerButton.setToggleState (plugin->isEnabled(), juce::dontSendNotification);

        for (auto* k : knobs)
        {
            k->refresh();
            k->setAlpha (plugin->isEnabled() ? 1.0f : 0.45f);
        }

        if (visualiser != nullptr)
        {
            visualiser->refresh();
            visualiser->setAlpha (plugin->isEnabled() ? 1.0f : 0.45f);
        }
    }

    int getPreferredWidth() const
    {
        if (visualiser == nullptr)
            return boxWidth;

        // Col disegno, la scheda e' larga quanto le manopole che mostra davvero
        const int columns = juce::jlimit (2, knobsPerRow, knobs.size());
        return 24 + DeviceVisualiser::preferredWidth + 10 + columns * (boxWidth - 24) / knobsPerRow;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onMenu != nullptr)
                onMenu();
        }
        else if (onClick != nullptr)
        {
            onClick (e.mods);
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 10.0f);

        // Fascia col colore della traccia in alto
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (getLocalBounds().removeFromTop (3));
            g.setColour (plugin->isEnabled() ? colour : colour.withSaturation (0.15f));
            g.fillRoundedRectangle (r, 10.0f);
        }

        if (selected)
        {
            g.setColour (colours::accent);
            g.drawRoundedRectangle (r.reduced (0.75f), 10.0f, 1.5f);
        }

        auto header = getLocalBounds().reduced (12, 10).removeFromTop (24);
        header.removeFromLeft (26);
        header.removeFromRight (26 + 50 + 6);

        auto badgeWidth = (int) juce::GlyphArrangement::getStringWidth (fonts::mono (10.0f), format) + 12;
        auto badge = header.removeFromRight (badgeWidth).withSizeKeepingCentre (badgeWidth, 18);
        g.setColour (colours::panelLight);
        g.fillRoundedRectangle (badge.toFloat(), 4.0f);
        g.setColour (colours::textDim);
        g.setFont (fonts::mono (10.0f));
        g.drawText (format, badge, juce::Justification::centred);

        // Gira in un processo suo: un punto accanto al formato (rosso se e' morto)
        if (isolated)
        {
            header.removeFromRight (4);
            auto dot = header.removeFromRight (8).withSizeKeepingCentre (6, 6).toFloat();
            g.setColour (error.isNotEmpty() ? colours::record : colours::accent);
            g.fillEllipse (dot);
        }

        g.setColour (plugin->isEnabled() ? colours::text : colours::textDim);
        g.setFont (fonts::ui (13.0f, fonts::Weight::medium));
        g.drawText (plugin->getName(), header.withTrimmedRight (6), juce::Justification::centredLeft, true);

        if (error.isNotEmpty())
        {
            g.setColour (colours::record);
            g.setFont (fonts::ui (11.5f));
            g.drawFittedText (error, getLocalBounds().reduced (12).withTrimmedTop (36), juce::Justification::topLeft, 4);
        }
        else if (knobs.isEmpty())
        {
            g.setColour (colours::textDim);
            g.setFont (fonts::ui (11.5f));
            g.drawFittedText ("Apri il device per vederne i controlli", getLocalBounds().reduced (12).withTrimmedTop (36),
                              juce::Justification::topLeft, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        auto header = r.removeFromTop (24);
        powerButton.setBounds (header.removeFromLeft (24));
        removeButton.setBounds (header.removeFromRight (24));
        header.removeFromRight (4);
        openButton.setBounds (header.removeFromRight (50).withSizeKeepingCentre (50, 22));

        r.removeFromTop (10);

        if (visualiser != nullptr)
        {
            visualiser->setBounds (r.removeFromLeft (DeviceVisualiser::preferredWidth).withTrimmedBottom (2));
            r.removeFromLeft (10);
        }

        const int knobW = (boxWidth - 24) / knobsPerRow, knobH = 70;

        for (int i = 0; i < knobs.size(); ++i)
        {
            const int row = i / knobsPerRow, col = i % knobsPerRow;
            auto area = juce::Rectangle<int> (r.getX() + col * knobW, r.getY() + row * (knobH + 4), knobW, knobH);
            knobs[i]->setBounds (area);
            knobs[i]->setVisible (area.getBottom() <= getHeight() - 6);
        }
    }

    te::Plugin::Ptr plugin;

private:
    te::RackType::Ptr rack;
    juce::Colour colour;
    juce::String format, error;
    bool selected = false, isolated = false;
    IconButton powerButton { "Attiva/disattiva", icons::power() };
    FlatButton openButton { "Apri", 11.5f };
    IconButton removeButton { "Rimuovi", icons::close() };
    juce::OwnedArray<ParameterKnob> knobs;
    std::unique_ptr<DeviceVisualiser> visualiser;
};

//==============================================================================
/** Le mappature di una macro: per ogni parametro, il valore con la macro a 0 e a 1. */
class MappingPanel final : public juce::Component
{
public:
    MappingPanel (te::RackType::Ptr r, int m) : rack (std::move (r)), macro (m)
    {
        rebuild();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (colours::text);
        g.setFont (fonts::ui (12.5f, fonts::Weight::medium));
        g.drawText (racks::getMacroName (*rack, macro), 12, 8, getWidth() - 24, 18, juce::Justification::centredLeft);

        if (rows.isEmpty())
        {
            g.setColour (colours::textDim);
            g.setFont (fonts::ui (11.5f));
            g.drawFittedText ("Nessun parametro. Clic destro su una manopola di un device del rack, "
                              "poi \"Assegna alla macro\".", getLocalBounds().reduced (12).withTrimmedTop (24),
                              juce::Justification::topLeft, 3);
            return;
        }

        g.setColour (colours::textDim);
        g.setFont (fonts::ui (10.5f));
        g.drawText ("Macro a 0", getWidth() - 12 - 24 - 2 * 74, 28, 70, 14, juce::Justification::centred);
        g.drawText ("Macro a 1", getWidth() - 12 - 24 - 74, 28, 70, 14, juce::Justification::centred);
    }

    void resized() override
    {
        int y = 46;

        for (auto* row : rows)
        {
            row->setBounds (12, y, getWidth() - 24, 26);
            y += 30;
        }
    }

private:
    struct Row final : public juce::Component
    {
        Row (MappingPanel& o, const racks::Mapping& mapping) : owner (o), param (mapping.parameter)
        {
            minimum.setValue (mapping.minimum * 100.0, juce::dontSendNotification);
            maximum.setValue (mapping.maximum * 100.0, juce::dontSendNotification);
            minimum.onChange = maximum.onChange = [this] (double) { apply(); };

            remove.setIconColour (colours::textDim);
            remove.onClick = [this]
            {
                auto& o2 = owner;
                auto r = o2.rack;
                auto p = param;
                undoable (r->edit, "Togli dalla macro", [&] { racks::unmap (*r, o2.macro, *p); });
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MappingPanel> (&o2)]
                {
                    if (safe != nullptr)
                        safe->rebuild();
                });
            };

            for (auto* c : std::initializer_list<juce::Component*> { &minimum, &maximum, &remove })
                addAndMakeVisible (c);
        }

        void apply()
        {
            undoable (owner.rack->edit, "Intervallo della macro", [this]
            {
                racks::setMappingRange (*owner.rack, owner.macro, *param,
                                        (float) minimum.getValue() / 100.0f, (float) maximum.getValue() / 100.0f);
            });
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (colours::textMid);
            g.setFont (fonts::ui (11.5f));
            g.drawText (parameterLabel (*param), getLocalBounds().withTrimmedRight (24 + 2 * 74), juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            remove.setBounds (r.removeFromRight (24).withSizeKeepingCentre (22, 22));
            maximum.setBounds (r.removeFromRight (74).reduced (2, 1));
            minimum.setBounds (r.removeFromRight (74).reduced (2, 1));
        }

        MappingPanel& owner;
        te::AutomatableParameter::Ptr param;
        DragNumber minimum { 0.0, 100.0, 1.0, 0, "%" }, maximum { 0.0, 100.0, 1.0, 0, "%" };
        IconButton remove { "Togli dalla macro", icons::close() };
    };

    void rebuild()
    {
        rows.clear();

        for (auto& mapping : racks::getMappings (*rack, macro))
            addAndMakeVisible (rows.add (new Row (*this, mapping)));

        setSize (420, rows.isEmpty() ? 78 : 52 + rows.size() * 30);
        resized();
        repaint();
    }

    te::RackType::Ptr rack;
    int macro;
    juce::OwnedArray<Row> rows;
};

//==============================================================================
/** Scheda del rack: catene a sinistra, otto macro a destra. */
class DeviceChainComponent::RackBox final : public juce::Component,
                                            public Refreshable
{
public:
    RackBox (te::Plugin::Ptr p, juce::Colour c, bool isTarget)
        : instance (std::move (p)), rack (racks::getRackType (*instance)), colour (c), targeted (isTarget)
    {
        powerButton.setActiveColour (colour);
        powerButton.setIconColour (colours::textDim);
        powerButton.onClick = [this]
        {
            undoable (instance->edit, "Attiva/disattiva rack", [this] { instance->setEnabled (! instance->isEnabled()); });
            refresh();
        };

        removeButton.setIconColour (colours::textDim);
        removeButton.onClick = [this]
        {
            juce::MessageManager::callAsync ([p = instance]
            {
                undoable (p->edit, "Rimuovi rack", [&] { racks::deleteRack (*p); });
            });
        };

        addChainButton.setBaseColour (colours::panelLight);
        addChainButton.setTooltip ("Aggiungi una catena in parallelo");
        addChainButton.onClick = [this]
        {
            undoable (instance->edit, "Aggiungi catena", [this]
            {
                const int chain = racks::addChain (*rack);

                if (onChainClicked != nullptr)
                    onChainClicked (chain);
            });
        };

        for (auto* b : std::initializer_list<juce::Component*> { &powerButton, &removeButton, &addChainButton, &chainViewport })
            addAndMakeVisible (b);

        chainViewport.setViewedComponent (&chainList, false);
        chainViewport.setScrollBarsShown (true, false);
        chainViewport.setScrollBarThickness (6);

        for (int i = 0; i < racks::getNumChains (*rack); ++i)
            chainList.addAndMakeVisible (rows.add (new ChainRow (*this, i)));

        for (int i = 0; i < racks::numMacros; ++i)
            addAndMakeVisible (macros.add (new MacroKnob (*this, i)));

        refresh();
    }

    std::function<void (int)> onChainClicked;
    std::function<void()> onMenu;

    void refresh() override
    {
        powerButton.setToggleState (instance->isEnabled(), juce::dontSendNotification);

        for (auto* r : rows)
            r->refresh();

        for (auto* m : macros)
            m->refresh();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onMenu != nullptr)
            onMenu();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < 34)
            rename();
    }

    void rename()
    {
        showRenameBox (*this, getLocalBounds().reduced (38, 8).removeFromTop (26).withTrimmedRight (20), rack->rackName.get(),
                       [r = rack] (const juce::String& n)
                       {
                           undoable (r->edit, "Rinomina rack", [&] { r->rackName = n; });
                       });
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (colours::panel);
        g.fillRoundedRectangle (r, 10.0f);

        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (getLocalBounds().removeFromTop (3));
            g.setColour (instance->isEnabled() ? colour : colour.withSaturation (0.15f));
            g.fillRoundedRectangle (r, 10.0f);
        }

        auto header = getLocalBounds().reduced (12, 10).removeFromTop (24);
        header.removeFromLeft (26);
        header.removeFromRight (30);

        auto badge = header.removeFromRight (44).withSizeKeepingCentre (44, 18);
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (badge.toFloat(), 4.0f);
        g.setColour (colour.brighter (0.3f));
        g.setFont (fonts::mono (10.0f));
        g.drawText ("RACK", badge, juce::Justification::centred);

        g.setColour (instance->isEnabled() ? colours::text : colours::textDim);
        g.setFont (fonts::ui (13.0f, fonts::Weight::medium));
        g.drawText (rack->rackName.get(), header.withTrimmedRight (6), juce::Justification::centredLeft, true);

        auto body = bodyArea();
        g.setColour (colours::textDim);
        g.setFont (fonts::ui (10.5f));
        g.drawText ("CATENE", body.getX(), body.getY(), 80, 14, juce::Justification::centredLeft);
        g.drawText ("MACRO", body.getX() + chainListWidth + 14, body.getY(), 80, 14, juce::Justification::centredLeft);

        g.setColour (colours::outline);
        g.drawVerticalLine (body.getX() + chainListWidth + 6, (float) body.getY(), (float) body.getBottom());
    }

    void resized() override
    {
        auto header = getLocalBounds().reduced (12, 10).removeFromTop (24);
        powerButton.setBounds (header.removeFromLeft (24));
        removeButton.setBounds (header.removeFromRight (24));

        auto body = bodyArea();
        auto chains = body.removeFromLeft (chainListWidth).withTrimmedTop (18);
        addChainButton.setBounds (chains.removeFromBottom (22).removeFromLeft (90));
        chains.removeFromBottom (4);
        chainViewport.setBounds (chains);

        chainList.setSize (chains.getWidth() - (rows.size() * chainRowHeight > chains.getHeight() ? 8 : 0),
                           rows.size() * chainRowHeight);

        for (int i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, i * chainRowHeight, chainList.getWidth(), chainRowHeight - 2);

        auto macroArea = body.withTrimmedLeft (14).withTrimmedTop (18);
        const int w = macroArea.getWidth() / 4, h = juce::jmin (74, macroArea.getHeight() / 2);

        for (int i = 0; i < macros.size(); ++i)
            macros[i]->setBounds (macroArea.getX() + (i % 4) * w, macroArea.getY() + (i / 4) * h, w, h);
    }

private:
    juce::Rectangle<int> bodyArea() const      { return getLocalBounds().reduced (12, 10).withTrimmedTop (34); }

    //==============================================================================
    struct ChainRow final : public juce::Component
    {
        ChainRow (RackBox& o, int i) : owner (o), index (i)
        {
            mute.setActiveColour (owner.colour);
            mute.setIconColour (colours::textDim);
            mute.setTooltip ("Accendi/spegni la catena");
            mute.onClick = [this]
            {
                undoable (owner.rack->edit, "Mute catena", [this]
                {
                    racks::setChainMuted (*owner.rack, index, ! racks::isChainMuted (*owner.rack, index));
                });
            };

            solo.setTooltip ("Solo: si sentono solo le catene in solo");
            solo.setOnColours (colours::accent, juce::Colours::black);
            solo.onClick = [this]
            {
                undoable (owner.rack->edit, "Solo catena", [this]
                {
                    racks::setChainSoloed (*owner.rack, index, ! racks::isChainSoloed (*owner.rack, index));
                });
            };

            volume.onChange = [this] (double db)
            {
                if (auto fader = racks::getChainFader (*owner.rack, index))
                    undoable (owner.rack->edit, "Volume catena", [&] { fader->setVolumeDb ((float) db); });
            };

            for (auto* c : std::initializer_list<juce::Component*> { &mute, &solo, &volume })
                addAndMakeVisible (c);

            refresh();
        }

        void refresh()
        {
            auto& r = *owner.rack;
            mute.setToggleState (! racks::isChainMuted (r, index), juce::dontSendNotification);
            solo.setToggleState (racks::isChainSoloed (r, index), juce::dontSendNotification);

            if (auto fader = racks::getChainFader (r, index))
                volume.setValue (fader->getVolumeDb(), juce::dontSendNotification);

            const bool nowSelected = racks::getSelectedChain (r) == index;
            const bool nowAudible = racks::isChainAudible (r, index);
            auto nowName = racks::getChainName (r, index);

            if (nowSelected != selected || nowAudible != audible || nowName != name)
            {
                selected = nowSelected;
                audible = nowAudible;
                name = nowName;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();

            if (selected)
            {
                g.setColour (owner.targeted ? colours::accent.withAlpha (0.22f) : colours::hover);
                g.fillRoundedRectangle (r, 5.0f);
            }

            g.setColour (audible ? colours::text : colours::textDim);
            g.setFont (fonts::ui (12.0f, selected ? fonts::Weight::medium : fonts::Weight::regular));
            g.drawText (name, nameArea(), juce::Justification::centredLeft, true);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            mute.setBounds (r.removeFromLeft (22).withSizeKeepingCentre (20, 20));
            volume.setBounds (r.removeFromRight (62).reduced (1, 1));
            r.removeFromRight (2);
            solo.setBounds (r.removeFromRight (22).withSizeKeepingCentre (20, 18));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
            {
                showMenu();
                return;
            }

            if (owner.onChainClicked != nullptr)
                owner.onChainClicked (index);
        }

        void mouseDoubleClick (const juce::MouseEvent&) override    { rename(); }

        void rename()
        {
            showRenameBox (*this, nameArea(), name, [r = owner.rack, i = index] (const juce::String& n)
            {
                undoable (r->edit, "Rinomina catena", [&] { racks::setChainName (*r, i, n); });
            });
        }

        void showMenu()
        {
            juce::PopupMenu m;
            juce::Component::SafePointer<ChainRow> safe (this);
            auto r = owner.rack;
            const int i = index;

            m.addItem ("Rinomina", [safe] { if (safe != nullptr) safe->rename(); });
            m.addItem ("Duplica", [r, i] { undoable (r->edit, "Duplica catena", [&] { racks::duplicateChain (*r, i); }); });
            m.addSeparator();
            m.addItem ("Elimina", racks::getNumChains (*r) > 1, false, [r, i]
            {
                undoable (r->edit, "Elimina catena", [&] { racks::removeChain (*r, i); });
            });
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        juce::Rectangle<int> nameArea() const     { return getLocalBounds().withTrimmedLeft (26).withTrimmedRight (62 + 2 + 22 + 4); }

        RackBox& owner;
        const int index;
        IconButton mute { "Catena accesa", icons::power() };
        FlatButton solo { "S", 11.0f };
        DragNumber volume { -70.0, 6.0, 0.1, 1, "dB" };
        juce::String name;
        bool selected = false, audible = true;
    };

    //==============================================================================
    struct MacroKnob final : public juce::Component
    {
        MacroKnob (RackBox& o, int i) : owner (o), index (i)
        {
            slider.setColour (juce::Slider::rotarySliderFillColourId, owner.colour);
            slider.onDragStart = [this] { if (auto m = racks::getMacro (*owner.rack, index)) m->parameterChangeGestureBegin(); };
            slider.onDragEnd = [this] { if (auto m = racks::getMacro (*owner.rack, index)) m->parameterChangeGestureEnd(); };
            slider.onValueChange = [this] { racks::setMacroValue (*owner.rack, index, (float) slider.getValue()); };
            slider.onPopupMenu = [this] { showMenu(); };
            addAndMakeVisible (slider);
            refresh();
        }

        void refresh()
        {
            auto& r = *owner.rack;

            if (! slider.isMouseButtonDown())
                slider.setValue (racks::getMacroValue (r, index), juce::dontSendNotification);

            auto nowName = racks::getMacroName (r, index);
            auto nowValue = percent (racks::getMacroValue (r, index));
            const int nowCount = (int) racks::getMappings (r, index).size();
            const bool nowShowValue = slider.isMouseOverOrDragging();

            if (nowName != name || nowValue != valueText || nowCount != count || nowShowValue != showValue)
            {
                showValue = nowShowValue;
                name = nowName;
                valueText = nowValue;
                count = nowCount;
                slider.setTooltip (name + (count == 0 ? juce::String (": nessun parametro (clic destro per le opzioni)")
                                                      : ": muove " + juce::String (count) + (count == 1 ? " parametro" : " parametri")
                                                            + " (clic destro per gli intervalli)"));
                slider.setAlpha (count > 0 ? 1.0f : 0.55f);
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            // Sotto la manopola il nome; col mouse sopra, il valore
            auto r = getLocalBounds().withTrimmedTop (38).removeFromTop (14);

            if (showValue)
            {
                g.setColour (colours::text);
                g.setFont (fonts::mono (10.0f));
                g.drawText (valueText, r, juce::Justification::centred, true);
                return;
            }

            g.setColour (count > 0 ? colours::text : colours::textDim);
            g.setFont (fonts::ui (10.5f));
            g.drawText (name, r, juce::Justification::centred, true);
        }

        void resized() override
        {
            slider.setBounds (getLocalBounds().removeFromTop (38).withSizeKeepingCentre (38, 38));
        }

        void showMenu()
        {
            juce::PopupMenu m;
            juce::Component::SafePointer<MacroKnob> safe (this);
            auto r = owner.rack;
            const int i = index;

            m.addItem ("Rinomina", [safe]
            {
                if (safe != nullptr)
                    showRenameBox (*safe, safe->getLocalBounds().withTrimmedTop (38).removeFromTop (18), safe->name,
                                   [r = safe->owner.rack, i = safe->index] (const juce::String& n)
                                   {
                                       undoable (r->edit, "Rinomina macro", [&] { racks::setMacroName (*r, i, n); });
                                   });
            });

            m.addItem ("Parametri e intervalli...", [safe, r, i]
            {
                if (safe != nullptr)
                    juce::CallOutBox::launchAsynchronously (std::make_unique<MappingPanel> (r, i),
                                                            safe->getScreenBounds(), nullptr);
            });

            m.addItem ("Togli tutti i parametri", count > 0, false, [r, i]
            {
                undoable (r->edit, "Togli dalla macro", [&]
                {
                    for (auto& mapping : racks::getMappings (*r, i))
                        racks::unmap (*r, i, *mapping.parameter);
                });
            });

            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        RackBox& owner;
        const int index;
        KnobSlider slider;
        juce::String name, valueText;
        int count = -1;
        bool showValue = false;
    };

    te::Plugin::Ptr instance;
    te::RackType::Ptr rack;
    juce::Colour colour;
    const bool targeted;
    IconButton powerButton { "Attiva/disattiva il rack", icons::power() };
    IconButton removeButton { "Rimuovi il rack", icons::close() };
    FlatButton addChainButton { "+ Catena", 11.5f };
    juce::Viewport chainViewport;
    juce::Component chainList;
    juce::OwnedArray<ChainRow> rows;
    juce::OwnedArray<MacroKnob> macros;
};

//==============================================================================
/** Al posto dei device di una catena vuota: dice come riempirla. */
class DeviceChainComponent::EmptyChainBox final : public juce::Component
{
public:
    EmptyChainBox (juce::String chain, bool isTarget) : chainName (std::move (chain)), targeted (isTarget) {}

    std::function<void()> onClick;

    void mouseDown (const juce::MouseEvent&) override
    {
        if (onClick != nullptr)
            onClick();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        juce::Path outline;
        outline.addRoundedRectangle (r, 10.0f);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (targeted ? colours::accent.withAlpha (0.7f) : colours::outline);
        g.fillPath (dashed);

        g.setColour (colours::textDim);
        g.setFont (fonts::ui (12.0f));
        g.drawFittedText (chainName + juce::String::fromUTF8 (" \xc3\xa8 vuota: ")
                              + (targeted ? "fai doppio clic su un device nel browser per metterlo qui"
                                          : "cliccala, poi fai doppio clic su un device nel browser"),
                          getLocalBounds().reduced (18), juce::Justification::centred, 4);
    }

private:
    juce::String chainName;
    bool targeted;
};

//==============================================================================
/** Contenuto scorrevole: sotto i device di un rack disegna la cornice che li raccoglie. */
class DeviceChainComponent::Content final : public juce::Component
{
public:
    struct Group
    {
        juce::Rectangle<int> area;
        bool targeted = false;
    };

    std::vector<Group> groups;
    std::function<void (const juce::MouseEvent&)> onBackgroundClick;

    void paint (juce::Graphics& g) override
    {
        for (auto& group : groups)
        {
            auto r = group.area.toFloat();
            g.setColour (colours::panelLight.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (group.targeted ? colours::accent.withAlpha (0.6f) : colours::outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (onBackgroundClick != nullptr)
            onBackgroundClick (e);
    }
};

//==============================================================================
DeviceChainComponent::DeviceChainComponent() : content (std::make_unique<Content>())
{
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (false, true);
    addAndMakeVisible (viewport);
    addChildComponent (unfreezeButton);
    unfreezeButton.onClick = [this]
    {
        if (track != nullptr)
            freeze::unfreeze (*track);

        rebuild();
    };

    content->onBackgroundClick = [this] (const juce::MouseEvent& e) { mouseDown (e.getEventRelativeTo (this)); };
    startTimerHz (30);
}

DeviceChainComponent::~DeviceChainComponent()
{
    items.clear();
}

void DeviceChainComponent::setTrack (te::AudioTrack* t)
{
    if (t != track.get())
    {
        selection.clear();
        targetRack = nullptr;
    }

    track = t;
    rebuild();
}

te::Plugin::Array DeviceChainComponent::getUserPlugins() const
{
    te::Plugin::Array result;

    if (track != nullptr)
        for (auto p : track->pluginList.getPlugins())
            if (! isMixerPlugin (p))
                result.add (p);

    return result;
}

juce::String DeviceChainComponent::signature() const
{
    juce::String s;

    for (auto p : getUserPlugins())
    {
        s << p->itemID.toString() << (p->isFrozen() ? "f" : "") << isolation::tag (*p) << ";";

        if (auto rack = racks::getRackType (*p))
        {
            const int chain = racks::getSelectedChain (*rack);
            s << "[" << racks::getNumChains (*rack) << ":" << chain << ":";

            for (auto inside : racks::getChainPlugins (*rack, chain))
                s << inside->itemID.toString() << isolation::tag (*inside) << ",";

            s << (rack == targetRack.get() ? "T" : "") << "]";
        }
    }

    s << trackColour().toString() << (isTrackFrozen() ? "F" : "");
    return s;
}

void DeviceChainComponent::timerCallback()
{
    // Il rack scelto per i nuovi device non c'e' piu' su questa traccia
    if (targetRack != nullptr && targetRackInstance() == nullptr)
        targetRack = nullptr;

    if (signature() != shownSignature)
    {
        rebuild();
        return;
    }

    for (auto& item : items)
        if (auto r = dynamic_cast<Refreshable*> (item.component.get()))
            r->refresh();
}

void DeviceChainComponent::rebuild()
{
    items.clear();
    content->groups.clear();
    shownSignature = signature();

    const auto colour = trackColour();
    const bool frozen = isTrackFrozen();
    unfreezeButton.setVisible (frozen);

    auto addDevice = [&] (te::Plugin* p, te::RackType* rack)
    {
        auto box = std::make_unique<DeviceBox> (p, colour, rack);
        te::Plugin::Ptr ptr (p);
        te::RackType::Ptr rackPtr (rack);

        box->onClick = [this, ptr, rackPtr] (const juce::ModifierKeys& mods)
        {
            if (rackPtr != nullptr)
                chainClicked (*rackPtr, racks::getSelectedChain (*rackPtr));
            else
                deviceClicked (*ptr, mods);
        };
        box->onMenu = [this, ptr] { showDeviceMenu (*ptr); };
        box->setSelected (rack == nullptr && selection.contains (p->itemID));

        // I device congelati sono fermi: si vedono ma non si toccano
        box->setEnabled (! p->isFrozen());
        box->setAlpha (p->isFrozen() ? 0.4f : 1.0f);

        const int w = box->getPreferredWidth();
        items.push_back ({ std::move (box), w, rack });
    };

    for (auto p : getUserPlugins())
    {
        auto rack = racks::getRackType (*p);

        if (rack == nullptr)
        {
            addDevice (p, nullptr);
            continue;
        }

        const bool targeted = rack == targetRack.get();
        auto box = std::make_unique<RackBox> (p, colour, targeted);
        te::Plugin::Ptr ptr (p);
        te::RackType::Ptr rackPtr (rack);
        box->onChainClicked = [this, rackPtr] (int chain) { chainClicked (*rackPtr, chain); };
        box->onMenu = [this, ptr] { showRackMenu (*ptr); };
        box->setEnabled (! p->isFrozen());
        box->setAlpha (p->isFrozen() ? 0.4f : 1.0f);
        items.push_back ({ std::move (box), rackBoxWidth, rack });

        const int chain = racks::getSelectedChain (*rack);
        auto inside = racks::getChainPlugins (*rack, chain);

        for (auto d : inside)
            addDevice (d, rack);

        if (inside.isEmpty())
        {
            auto empty = std::make_unique<EmptyChainBox> (racks::getChainName (*rack, chain), targeted);
            empty->onClick = [this, rackPtr, chain] { chainClicked (*rackPtr, chain); };
            items.push_back ({ std::move (empty), emptyChainWidth, rack });
        }
    }

    for (auto& item : items)
        content->addAndMakeVisible (*item.component);

    resized();
    repaint();
}

void DeviceChainComponent::layoutItems()
{
    content->groups.clear();

    const int h = juce::jmax (0, viewport.getHeight() - viewport.getScrollBarThickness());
    int x = 0;
    te::RackType* currentGroup = nullptr;

    auto closeGroup = [&]
    {
        if (currentGroup != nullptr)
        {
            content->groups.back().area.setRight (x - gap + 5);
            x += 6;
        }

        currentGroup = nullptr;
    };

    for (auto& item : items)
    {
        // Un rack e i device della sua catena stanno in una cornice
        const bool startsRack = dynamic_cast<RackBox*> (item.component.get()) != nullptr;

        if (startsRack || item.rack != currentGroup)
            closeGroup();

        if (startsRack)
        {
            currentGroup = item.rack;
            content->groups.push_back ({ { x, 0, 0, h }, item.rack == targetRack.get() });
            x += 5;
        }

        const int inset = currentGroup != nullptr ? 5 : 0;
        item.component->setBounds (x, inset, item.width, h - 2 * inset);
        x += item.width + gap;
    }

    closeGroup();
    content->setSize (juce::jmax (getWidth(), x), h);
}

bool DeviceChainComponent::isTrackFrozen() const
{
    return track != nullptr && freeze::isFrozen (*track);
}

juce::Colour DeviceChainComponent::trackColour() const
{
    return track != nullptr && ! track->getColour().isTransparent() ? track->getColour() : colours::accent;
}

void DeviceChainComponent::status (const juce::String& s)
{
    if (onStatus != nullptr)
        onStatus (s);
}

bool DeviceChainComponent::canEdit()
{
    if (track == nullptr)
    {
        status ("Seleziona prima una traccia");
        return false;
    }

    if (isTrackFrozen())
    {
        status (freezeui::frozenNotice (*track));
        return false;
    }

    return true;
}

int DeviceChainComponent::insertionIndex() const
{
    // Prima del volume/pan e del misuratore, che restano in fondo alla catena.
    int index = 0;

    for (auto p : track->pluginList.getPlugins())
    {
        if (dynamic_cast<te::VolumeAndPanPlugin*> (p) != nullptr || dynamic_cast<te::LevelMeterPlugin*> (p) != nullptr)
            break;

        ++index;
    }

    return index;
}

//==============================================================================
te::Plugin* DeviceChainComponent::targetRackInstance() const
{
    return track != nullptr && targetRack != nullptr ? racks::findRackInstance (*track, *targetRack) : nullptr;
}

void DeviceChainComponent::clearTarget()
{
    targetRack = nullptr;
}

bool DeviceChainComponent::insertIntoTarget (const te::Plugin::Ptr& plugin)
{
    if (targetRackInstance() == nullptr)
        return false;

    return racks::insertPlugin (*targetRack, racks::getSelectedChain (*targetRack), plugin);
}

juce::String DeviceChainComponent::getInsertTargetName() const
{
    if (targetRackInstance() == nullptr)
        return track != nullptr ? track->getName() : juce::String();

    return targetRack->rackName.get() + juce::String::fromUTF8 (" \xe2\x80\xba ")
            + racks::getChainName (*targetRack, racks::getSelectedChain (*targetRack));
}

void DeviceChainComponent::deviceClicked (te::Plugin& p, const juce::ModifierKeys& mods)
{
    clearTarget();

    if (mods.isShiftDown() || mods.isCommandDown())
    {
        if (selection.contains (p.itemID))
            selection.removeFirstMatchingValue (p.itemID);
        else
            selection.add (p.itemID);
    }
    else
    {
        selection.clearQuick();
        selection.add (p.itemID);
    }

    for (auto& item : items)
        if (auto box = dynamic_cast<DeviceBox*> (item.component.get()); box != nullptr && item.rack == nullptr)
            box->setSelected (selection.contains (box->plugin->itemID));
}

void DeviceChainComponent::chainClicked (te::RackType& rack, int chain)
{
    racks::setSelectedChain (rack, chain);
    targetRack = &rack;
    selection.clearQuick();

    // Il rack stesso risulta selezionato, per Ctrl+Maiusc+G
    if (auto instance = targetRackInstance())
        selection.add (instance->itemID);

    status ("I device aggiunti dal browser vanno in " + getInsertTargetName());
}

void DeviceChainComponent::mouseDown (const juce::MouseEvent& e)
{
    // Clic sullo sfondo: niente selezione, i nuovi device tornano sulla traccia
    if (e.mods.isPopupMenu())
    {
        if (track == nullptr)
            return;

        juce::PopupMenu m;
        m.addItem ("Aggiungi un rack vuoto", [this] { addEmptyRack(); });
        m.addItem ("Metti tutti i device in un rack", ! getUserPlugins().isEmpty(), false, [this]
        {
            selection.clearQuick();
            groupSelection();
        });
        m.showMenuAsync (juce::PopupMenu::Options());
        return;
    }

    selection.clearQuick();
    clearTarget();

    for (auto& item : items)
        if (auto box = dynamic_cast<DeviceBox*> (item.component.get()))
            box->setSelected (false);
}

namespace
{
    /** Voci per i plugin esterni: processo separato e riavvio. */
    void addIsolationItems (juce::PopupMenu& m, te::Plugin& p)
    {
        auto external = dynamic_cast<te::ExternalPlugin*> (&p);

        if (external == nullptr || external->getAudioPluginInstance() == nullptr)
            return;

        te::Plugin::Ptr ptr (&p);
        const bool isolated = isolation::isIsolated (p);
        const bool chosenMode = sandbox::getPolicy() == sandbox::Policy::chosen;

        m.addSeparator();

        if (isolation::hasCrashed (p))
            m.addItem ("Riavvia il plugin", [ptr]
            {
                if (auto e = dynamic_cast<te::ExternalPlugin*> (ptr.get()))
                    isolation::reload (*e);
            });

        m.addItem (chosenMode ? juce::String ("Esegui in un processo separato")
                              : "Processo separato: " + sandbox::policyName (sandbox::getPolicy()).toLowerCase() + " (menu principale)",
                   chosenMode, isolated, [ptr, isolated]
        {
            if (auto e = dynamic_cast<te::ExternalPlugin*> (ptr.get()))
            {
                sandbox::setChosen (e->desc, ! isolated);
                saveSandboxSettings (e->edit.engine);
                isolation::reload (*e);
            }
        });
    }
}

void DeviceChainComponent::showDeviceMenu (te::Plugin& p)
{
    juce::PopupMenu m;
    te::Plugin::Ptr ptr (&p);

    if (auto rack = racks::getContainingRack (p))
    {
        te::RackType::Ptr r (rack);
        m.addItem ("Apri", [ptr] { ptr->showWindowExplicitly(); });
        addIsolationItems (m, p);
        m.addSeparator();
        m.addItem ("Rimuovi dalla catena", [ptr, r]
        {
            undoable (ptr->edit, "Rimuovi device", [&] { racks::removePlugin (*r, *ptr); });
        });
    }
    else
    {
        if (! selection.contains (p.itemID))
            deviceClicked (p, {});

        const int count = selection.size();
        m.addItem ("Apri", [ptr] { ptr->showWindowExplicitly(); });
        m.addItem (count > 1 ? "Raggruppa i " + juce::String (count) + " device in un rack" : juce::String ("Metti in un rack"),
                   [this] { groupSelection(); });
        addIsolationItems (m, p);
        m.addSeparator();
        m.addItem ("Rimuovi", [ptr] { undoable (ptr->edit, "Rimuovi device", [&] { ptr->deleteFromParent(); }); });
    }

    m.showMenuAsync (juce::PopupMenu::Options());
}

void DeviceChainComponent::showRackMenu (te::Plugin& p)
{
    juce::PopupMenu m;
    te::Plugin::Ptr ptr (&p);
    te::RackType::Ptr rack (racks::getRackType (p));

    if (rack == nullptr)
        return;

    m.addItem ("Rinomina", [this, ptr]
    {
        for (auto& item : items)
            if (auto box = dynamic_cast<RackBox*> (item.component.get()); box != nullptr && item.rack == racks::getRackType (*ptr))
                box->rename();
    });
    m.addItem ("Aggiungi una catena", [this, rack]
    {
        undoable (rack->edit, "Aggiungi catena", [&] { chainClicked (*rack, racks::addChain (*rack)); });
    });
    m.addItem ("Separa il rack", racks::canUngroup (p), false, [ptr]
    {
        undoable (ptr->edit, "Separa il rack", [&] { racks::ungroup (*ptr); });
    });
    m.addSeparator();
    m.addItem ("Rimuovi il rack", [ptr] { undoable (ptr->edit, "Rimuovi rack", [&] { racks::deleteRack (*ptr); }); });
    m.showMenuAsync (juce::PopupMenu::Options());
}

void DeviceChainComponent::groupSelection()
{
    if (! canEdit())
        return;

    te::Plugin::Array chosen;

    for (auto p : getUserPlugins())
        if (selection.isEmpty() || selection.contains (p->itemID))
            chosen.add (p);

    if (chosen.isEmpty())
    {
        status ("Non ci sono device da mettere in un rack");
        return;
    }

    for (auto p : chosen)
        if (racks::isRack (*p) || ! te::RackType::isPluginAllowed (p))
        {
            status ("\"" + p->getName() + juce::String::fromUTF8 ("\" non pu\xc3\xb2 andare dentro un rack"));
            return;
        }

    te::Plugin::Ptr instance;
    undoable (track->edit, "Raggruppa in un rack", [&] { instance = racks::group (*track, chosen); });

    if (instance == nullptr)
        return;

    selection.clearQuick();
    selection.add (instance->itemID);
    targetRack = racks::getRackType (*instance);
    status (juce::String (chosen.size()) + (chosen.size() == 1 ? " device messo" : " device messi")
             + " in un rack. Ctrl+Maiusc+G per separarlo");
}

void DeviceChainComponent::ungroupSelection()
{
    if (! canEdit())
        return;

    te::Plugin* chosen = targetRackInstance();

    for (auto p : getUserPlugins())
        if (chosen == nullptr && racks::isRack (*p) && (selection.contains (p->itemID) || selection.isEmpty()))
            chosen = p;

    if (chosen == nullptr)
    {
        status ("Seleziona un rack da separare");
        return;
    }

    if (! racks::canUngroup (*chosen))
    {
        status ("Si separa solo un rack con una catena: elimina prima le altre");
        return;
    }

    te::Plugin::Ptr keep (chosen);
    undoable (track->edit, "Separa il rack", [&] { racks::ungroup (*keep); });
    selection.clearQuick();
    clearTarget();
    status ("Rack separato: i device sono di nuovo sulla traccia");
}

void DeviceChainComponent::addEmptyRack()
{
    if (! canEdit())
        return;

    te::Plugin::Ptr instance;
    undoable (track->edit, "Aggiungi rack", [&] { instance = racks::createRack (*track, insertionIndex()); });

    if (auto rack = instance != nullptr ? racks::getRackType (*instance) : nullptr)
    {
        chainClicked (*rack, 0);
        status ("Rack aggiunto: fai doppio clic su un device nel browser per metterlo nella catena");
    }
}

//==============================================================================
juce::String DeviceChainComponent::getHintAt (juce::Point<int>)
{
    if (track == nullptr || items.empty())
        return {};

    return juce::String::fromUTF8 ("Clic sul titolo per selezionare un device (Maiusc per pi\xc3\xb9 di uno), "
                                   "Ctrl+G per metterli in un rack. Clic destro per le opzioni");
}

void DeviceChainComponent::paint (juce::Graphics& g)
{
    g.fillAll (colours::sidebar);
    const bool frozen = isTrackFrozen();

    if (frozen)
    {
        auto banner = getLocalBounds().removeFromTop (frozenBannerHeight).reduced (0, 4).toFloat();
        g.setColour (freezeui::iceColour().withAlpha (0.1f));
        g.fillRoundedRectangle (banner, 6.0f);
        freezeui::drawSnowflake (g, banner.removeFromLeft (30.0f).withSizeKeepingCentre (14.0f, 14.0f), freezeui::iceColour());
        g.setColour (colours::textMid);
        g.setFont (fonts::ui (12.0f));
        g.drawText (juce::String::fromUTF8 ("Traccia congelata: suona dal file e i device prima del fader sono fermi. "
                                            "Scongelala per modificarli."),
                    banner.withTrimmedRight (110.0f), juce::Justification::centredLeft, true);
    }

    if (track != nullptr && ! items.empty())
        return;

    auto r = getLocalBounds().withTrimmedTop (frozen ? frozenBannerHeight : 0).reduced (0, 4);
    auto hint = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 20, 420), juce::jmin (r.getHeight() - 8, 120));
    g.setColour (colours::outline);
    g.drawRoundedRectangle (hint.toFloat(), 10.0f, 1.0f);
    g.setColour (colours::textDim);
    g.setFont (fonts::ui (12.5f));
    g.drawFittedText (track == nullptr ? "Seleziona una traccia"
                                       : "Fai doppio clic su un device o un plugin nel browser per aggiungerlo a \""
                                            + track->getName() + "\"",
                      hint.reduced (16), juce::Justification::centred, 3);
}

void DeviceChainComponent::resized()
{
    auto area = getLocalBounds();

    if (unfreezeButton.isVisible())
    {
        auto banner = area.removeFromTop (frozenBannerHeight).reduced (0, 4);
        unfreezeButton.setBounds (banner.removeFromRight (96).reduced (4, 3));
        area.removeFromTop (4);
    }

    viewport.setBounds (area);
    layoutItems();
}

} // namespace sonora
