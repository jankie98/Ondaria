/*
    Sonora - DAW open source
    Copyright (C) 2026 Sonora contributors
    SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "DeviceVisualiser.h"
#include "Theme.h"

namespace te = tracktion;

namespace sonora
{

namespace
{
    constexpr float minFreq = 20.0f, maxFreq = 20000.0f;

    float xForFreq (juce::Rectangle<float> r, float f)
    {
        return r.getX() + r.getWidth() * std::log (f / minFreq) / std::log (maxFreq / minFreq);
    }

    float freqForX (juce::Rectangle<float> r, float x)
    {
        const float p = juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth());
        return minFreq * std::pow (maxFreq / minFreq, p);
    }

    float yForDb (juce::Rectangle<float> r, float db, float minDb, float maxDb)
    {
        return juce::jmap (db, minDb, maxDb, r.getBottom(), r.getY());
    }

    float dbForY (juce::Rectangle<float> r, float y, float minDb, float maxDb)
    {
        return juce::jmap (y, r.getBottom(), r.getY(), minDb, maxDb);
    }

    /** Guadagno in dB di un filtro biquadratico a una frequenza. */
    float magnitudeDb (const juce::IIRCoefficients& c, double freq, double sampleRate)
    {
        const auto* k = c.coefficients;   // b0 b1 b2 a1 a2, gia' normalizzati
        const std::complex<double> z = std::polar (1.0, -juce::MathConstants<double>::twoPi * freq / sampleRate);
        const auto num = (double) k[0] + (double) k[1] * z + (double) k[2] * z * z;
        const auto den = 1.0 + (double) k[3] * z + (double) k[4] * z * z;
        return (float) juce::Decibels::gainToDecibels (std::abs (num) / std::abs (den), -120.0);
    }

    double sampleRateOf (te::Plugin& p)
    {
        const double sr = p.edit.engine.getDeviceManager().getSampleRate();
        return sr > 0.0 ? sr : 44100.0;
    }

    juce::String freqText (float f)
    {
        return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz"
                            : juce::String (juce::roundToInt (f)) + " Hz";
    }

    /** Righe di frequenza (100 Hz, 1 kHz, 10 kHz) e di livello. */
    void drawFreqGrid (juce::Graphics& g, juce::Rectangle<float> r, std::initializer_list<float> dbLines, float minDb, float maxDb)
    {
        g.setColour (colours::divider.brighter (0.15f));

        for (float f : { 100.0f, 1000.0f, 10000.0f })
            g.fillRect (xForFreq (r, f), r.getY(), 1.0f, r.getHeight());

        for (float db : dbLines)
        {
            g.setColour (juce::approximatelyEqual (db, 0.0f) ? colours::outline : colours::divider.brighter (0.15f));
            g.fillRect (r.getX(), yForDb (r, db, minDb, maxDb), r.getWidth(), 1.0f);
        }

        g.setColour (colours::textDim.withAlpha (0.6f));
        g.setFont (fonts::mono (9.0f));

        for (auto [f, label] : { std::pair { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
            g.drawText (label, juce::Rectangle<float> (xForFreq (r, f) + 2.0f, r.getBottom() - 11.0f, 30.0f, 10.0f),
                        juce::Justification::centredLeft);
    }

    /** Curva piena (dal livello 0 dB) piu' il contorno. */
    void drawCurve (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, float zeroY,
                    const std::function<float (float x)>& yAtX)
    {
        juce::Path line, fill;
        fill.startNewSubPath (r.getX(), zeroY);

        for (float x = r.getX(); x <= r.getRight(); x += 1.0f)
        {
            const float y = juce::jlimit (r.getY() - 2.0f, r.getBottom() + 2.0f, yAtX (x));

            if (line.isEmpty())
                line.startNewSubPath (x, y);
            else
                line.lineTo (x, y);

            fill.lineTo (x, y);
        }

        fill.lineTo (r.getRight(), zeroY);
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.32f), 0.0f, r.getY(),
                                                 colour.withAlpha (0.04f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawHandle (juce::Graphics& g, juce::Point<float> p, juce::Colour colour, bool active, const juce::String& label)
    {
        const float radius = active ? 6.5f : 5.0f;

        if (active)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (radius * 4.0f, radius * 4.0f).withCentre (p));
        }

        g.setColour (active ? colours::text : colour.brighter (0.4f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p));

        if (label.isNotEmpty())
        {
            g.setColour (colours::ink);
            g.setFont (fonts::ui (8.5f, fonts::Weight::semibold));
            g.drawText (label, juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (p), juce::Justification::centred);
        }
    }

    /** Valore riassuntivo da mostrare in alto mentre si trascina o si passa sopra. */
    void drawReadout (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
    {
        if (text.isEmpty())
            return;

        g.setFont (fonts::mono (10.0f));
        const float w = juce::GlyphArrangement::getStringWidth (fonts::mono (10.0f), text) + 10.0f;
        auto box = juce::Rectangle<float> (r.getRight() - w, r.getY(), w, 15.0f);
        g.setColour (colours::background.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (colours::text);
        g.drawText (text, box, juce::Justification::centred);
    }

    /** Somma rapida dei valori: se cambia, il disegno va rifatto. */
    template <typename... Values>
    double fingerprint (Values... v)
    {
        double h = 0.0, k = 1.0;
        ((h += (double) v * (k *= 1.618)), ...);
        return h;
    }

    /** Modifica di un parametro con trascinamento: una sola voce di annulla. */
    struct ParamGesture
    {
        std::vector<te::AutomatableParameter::Ptr> params;

        void begin (te::Plugin& plugin, const juce::String& name, std::initializer_list<te::AutomatableParameter*> ps)
        {
            end();
            plugin.edit.getUndoManager().beginNewTransaction (name);

            for (auto* p : ps)
            {
                p->parameterChangeGestureBegin();
                params.emplace_back (p);
            }
        }

        void end()
        {
            for (auto& p : params)
                p->parameterChangeGestureEnd();

            params.clear();
        }

        ~ParamGesture()     { end(); }
    };
}

void DeviceVisualiser::paintBackground (juce::Graphics& g) const
{
    auto r = getLocalBounds().toFloat();
    g.setColour (colours::background.withAlpha (0.7f));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colours::divider);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
}

//==============================================================================
/** EQ a 4 bande: curva di risposta reale e un punto per banda da trascinare. */
class EqualiserVisualiser final : public DeviceVisualiser
{
public:
    EqualiserVisualiser (te::EqualiserPlugin& p, juce::Colour c) : DeviceVisualiser (c), eq (p)
    {
        bands = { Band { p.loFreq, p.loGain, p.loQ, Band::lowShelf },
                  Band { p.midFreq1, p.midGain1, p.midQ1, Band::peak },
                  Band { p.midFreq2, p.midGain2, p.midQ2, Band::peak },
                  Band { p.hiFreq, p.hiGain, p.hiQ, Band::highShelf } };
        setRepaintsOnMouseActivity (true);
        setTooltip ("Trascina i punti per frequenza e guadagno, rotella per la larghezza, doppio clic per azzerare");
    }

    void refresh() override
    {
        double h = 0.0;

        for (auto& b : bands)
            h += fingerprint (b.freq->getCurrentValue(), b.gain->getCurrentValue(), b.q->getCurrentValue());

        if (! juce::approximatelyEqual (h, lastFingerprint))
        {
            lastFingerprint = h;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBackground (g);
        auto r = plotArea();
        drawFreqGrid (g, r, { -12.0f, 0.0f, 12.0f }, minDb, maxDb);

        const double sr = sampleRateOf (eq);
        std::array<juce::IIRCoefficients, 4> coeffs;

        for (size_t i = 0; i < bands.size(); ++i)
            coeffs[i] = bands[i].coefficients (sr);

        auto dbAt = [&] (float f)
        {
            float db = 0.0f;

            for (size_t i = 0; i < bands.size(); ++i)
                if (! juce::approximatelyEqual (bands[i].gain->getCurrentValue(), 0.0f))
                    db += magnitudeDb (coeffs[i], f, sr);

            return db;
        };

        const float zeroY = yForDb (r, 0.0f, minDb, maxDb);
        drawCurve (g, r, colour, zeroY, [&] (float x) { return yForDb (r, dbAt (freqForX (r, x)), minDb, maxDb); });

        const int hot = dragging >= 0 ? dragging : (isMouseOver() ? handleAt (getMouseXYRelative().toFloat()) : -1);

        for (int i = 0; i < (int) bands.size(); ++i)
            drawHandle (g, handlePos (i), colour, i == hot, juce::String (i + 1));

        if (hot >= 0)
        {
            auto& b = bands[(size_t) hot];
            const float gain = b.gain->getCurrentValue();
            drawReadout (g, r, freqText (b.freq->getCurrentValue()) + "  " + (gain > 0 ? "+" : "") + juce::String (gain, 1) + " dB");
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = handleAt (e.position);

        if (dragging >= 0)
        {
            auto& b = bands[(size_t) dragging];
            gesture.begin (eq, "EQ", { b.freq.get(), b.gain.get() });
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0)
            return;

        auto r = plotArea();
        auto& b = bands[(size_t) dragging];
        b.freq->setParameter (juce::jlimit (te::EqualiserPlugin::minFreq, te::EqualiserPlugin::maxFreq, freqForX (r, e.position.x)),
                              juce::sendNotification);
        b.gain->setParameter (juce::jlimit (te::EqualiserPlugin::minGain, te::EqualiserPlugin::maxGain, dbForY (r, e.position.y, minDb, maxDb)),
                              juce::sendNotification);
        refresh();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        gesture.end();
        dragging = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (auto i = handleAt (e.position); i >= 0)
        {
            eq.edit.getUndoManager().beginNewTransaction ("EQ");
            bands[(size_t) i].gain->setParameter (0.0f, juce::sendNotification);
            refresh();
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (auto i = handleAt (e.position); i >= 0)
        {
            auto& q = bands[(size_t) i].q;
            const float next = q->getCurrentValue() * (w.deltaY > 0 ? 1.15f : 1.0f / 1.15f);
            q->setParameter (juce::jlimit (te::EqualiserPlugin::minQ, te::EqualiserPlugin::maxQ, next), juce::sendNotification);
            refresh();
        }
        else
        {
            DeviceVisualiser::mouseWheelMove (e, w);
        }
    }

private:
    struct Band
    {
        enum Type { lowShelf, peak, highShelf };

        te::AutomatableParameter::Ptr freq, gain, q;
        Type type;

        juce::IIRCoefficients coefficients (double sr) const
        {
            const float f = juce::jlimit (minFreq, (float) sr * 0.49f, freq->getCurrentValue());
            const float g = juce::Decibels::decibelsToGain (gain->getCurrentValue());
            const float qv = q->getCurrentValue();

            switch (type)
            {
                case lowShelf:  return juce::IIRCoefficients::makeLowShelf (sr, f, qv, g);
                case highShelf: return juce::IIRCoefficients::makeHighShelf (sr, f, qv, g);
                case peak:      break;
            }

            return juce::IIRCoefficients::makePeakFilter (sr, f, qv, g);
        }
    };

    static constexpr float minDb = -21.0f, maxDb = 21.0f;

    juce::Point<float> handlePos (int i) const
    {
        auto r = plotArea();
        auto& b = bands[(size_t) i];
        return { xForFreq (r, b.freq->getCurrentValue()), yForDb (r, b.gain->getCurrentValue(), minDb, maxDb) };
    }

    int handleAt (juce::Point<float> p) const
    {
        int best = -1;
        float bestDist = 11.0f;

        for (int i = 0; i < (int) bands.size(); ++i)
            if (auto d = handlePos (i).getDistanceFrom (p); d < bestDist)
                std::tie (best, bestDist) = std::pair { i, d };

        return best;
    }

    te::EqualiserPlugin& eq;
    std::array<Band, 4> bands;
    int dragging = -1;
    double lastFingerprint = 0.0;
    ParamGesture gesture;
};

//==============================================================================
/** Filtro passa basso / passa alto: curva e frequenza di taglio da trascinare. */
class FilterVisualiser final : public DeviceVisualiser
{
public:
    FilterVisualiser (te::LowPassPlugin& p, juce::Colour c) : DeviceVisualiser (c), filter (p)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip ("Trascina per la frequenza di taglio, doppio clic per passare da passa basso a passa alto");
    }

    void refresh() override
    {
        const double h = fingerprint (filter.frequency->getCurrentValue(), filter.isLowPass() ? 1.0f : 0.0f);

        if (! juce::approximatelyEqual (h, lastFingerprint))
        {
            lastFingerprint = h;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBackground (g);
        auto r = plotArea();
        drawFreqGrid (g, r, { -24.0f, -12.0f, 0.0f }, minDb, maxDb);

        const double sr = sampleRateOf (filter);
        const float cutoff = juce::jlimit (minFreq, (float) sr * 0.49f, filter.frequency->getCurrentValue());
        const auto c = filter.isLowPass() ? juce::IIRCoefficients::makeLowPass (sr, cutoff)
                                          : juce::IIRCoefficients::makeHighPass (sr, cutoff);

        drawCurve (g, r, colour, r.getBottom(),
                   [&] (float x) { return yForDb (r, magnitudeDb (c, freqForX (r, x), sr), minDb, maxDb); });

        const bool hot = dragging || (isMouseOver() && std::abs (getMouseXYRelative().x - handlePos().x) < 12.0f);
        drawHandle (g, handlePos(), colour, hot, {});

        g.setColour (colours::textDim);
        g.setFont (fonts::ui (9.5f, fonts::Weight::medium));
        g.drawText (filter.isLowPass() ? "Passa basso" : "Passa alto", r.withHeight (14.0f), juce::Justification::topLeft);

        if (hot)
            drawReadout (g, r, freqText (cutoff));
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = true;
        gesture.begin (filter, "Filtro", { filter.frequency.get() });
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        filter.frequency->setParameter (juce::jlimit (10.0f, 22000.0f, freqForX (plotArea(), e.position.x)), juce::sendNotification);
        refresh();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        gesture.end();
        dragging = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        filter.edit.getUndoManager().beginNewTransaction ("Filtro");
        filter.mode = filter.isLowPass() ? "highpass" : "lowpass";
        refresh();
    }

private:
    static constexpr float minDb = -36.0f, maxDb = 9.0f;

    juce::Point<float> handlePos() const
    {
        auto r = plotArea();
        return { xForFreq (r, filter.frequency->getCurrentValue()), yForDb (r, -3.0f, minDb, maxDb) };
    }

    te::LowPassPlugin& filter;
    bool dragging = false;
    double lastFingerprint = 0.0;
    ParamGesture gesture;
};

//==============================================================================
/** Compressore: curva ingresso/uscita con soglia da trascinare e rapporto con la rotella. */
class CompressorVisualiser final : public DeviceVisualiser
{
public:
    CompressorVisualiser (te::CompressorPlugin& p, juce::Colour c) : DeviceVisualiser (c), comp (p)
    {
        setRepaintsOnMouseActivity (true);
        setTooltip ("Trascina per la soglia, rotella per il rapporto");
    }

    void refresh() override
    {
        const double h = fingerprint (comp.thresholdGain.getCurrentValue(), comp.ratio.getCurrentValue(), comp.outputDb.getCurrentValue());

        if (! juce::approximatelyEqual (h, lastFingerprint))
        {
            lastFingerprint = h;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBackground (g);
        auto r = square();

        // Griglia ogni 12 dB e diagonale 1:1
        g.setColour (colours::divider.brighter (0.15f));

        for (float db = -48.0f; db < 0.0f; db += 12.0f)
        {
            g.fillRect (xForIn (r, db), r.getY(), 1.0f, r.getHeight());
            g.fillRect (r.getX(), yForOut (r, db), r.getWidth(), 1.0f);
        }

        g.setColour (colours::outline);
        g.drawLine ({ r.getBottomLeft(), r.getTopRight() }, 1.0f);

        // Curva di trasferimento
        const float makeup = comp.outputDb.getCurrentValue();
        juce::Path curve;

        for (float x = r.getX(); x <= r.getRight(); x += 1.0f)
        {
            const float in = juce::jmap (x, r.getX(), r.getRight(), minDb, 0.0f);
            const float y = juce::jlimit (r.getY(), r.getBottom(), yForOut (r, outputFor (in) + makeup));
            curve.isEmpty() ? curve.startNewSubPath (x, y) : curve.lineTo (x, y);
        }

        auto area = curve;
        area.lineTo (r.getRight(), r.getBottom());
        area.lineTo (r.getX(), r.getBottom());
        area.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.28f), 0.0f, r.getY(), colour.withAlpha (0.03f), 0.0f, r.getBottom(), false));
        g.fillPath (area);
        g.setColour (colour);
        g.strokePath (curve, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const bool hot = dragging || (isMouseOver() && handlePos().getDistanceFrom (getMouseXYRelative().toFloat()) < 14.0f);
        drawHandle (g, handlePos(), colour, hot, {});

        // Testo a destra del quadrato
        auto info = plotArea().withLeft (r.getRight() + 8.0f);
        g.setFont (fonts::ui (9.5f, fonts::Weight::medium));
        g.setColour (colours::textDim);
        g.drawText ("Soglia", info.removeFromTop (13.0f), juce::Justification::centredLeft);
        g.setColour (hot ? colours::text : colours::textMid);
        g.setFont (fonts::mono (11.0f));
        g.drawText (juce::String (thresholdDb(), 1) + " dB", info.removeFromTop (15.0f), juce::Justification::centredLeft);
        info.removeFromTop (8.0f);
        g.setFont (fonts::ui (9.5f, fonts::Weight::medium));
        g.setColour (colours::textDim);
        g.drawText ("Rapporto", info.removeFromTop (13.0f), juce::Justification::centredLeft);
        g.setColour (colours::textMid);
        g.setFont (fonts::mono (11.0f));
        const float rat = comp.ratio.getCurrentValue();
        g.drawText (rat > 0.001f ? juce::String (1.0f / rat, 1) + ":1" : juce::String ("inf:1"), info.removeFromTop (15.0f),
                    juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragging = true;
        gesture.begin (comp, "Compressore", { &comp.thresholdGain.getParameter() });
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto r = square();
        const float db = juce::jlimit (-40.0f, 0.0f, juce::jmap (e.position.x, r.getX(), r.getRight(), minDb, 0.0f));
        comp.thresholdGain.setParameter (juce::Decibels::decibelsToGain (db), juce::sendNotification);
        refresh();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        gesture.end();
        dragging = false;
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        // Il parametro e' 1/rapporto: rotella in su = compressione piu' forte
        comp.edit.getUndoManager().beginNewTransaction ("Compressore");
        const float current = juce::jmax (0.05f, comp.ratio.getCurrentValue());
        comp.ratio.setParameter (juce::jlimit (0.05f, 0.95f, current * (w.deltaY > 0 ? 1.0f / 1.2f : 1.2f)), juce::sendNotification);
        refresh();
    }

private:
    static constexpr float minDb = -60.0f;

    juce::Rectangle<float> square() const
    {
        auto r = plotArea();
        return r.withWidth (juce::jmin (r.getHeight(), r.getWidth() - 70.0f));
    }

    static float xForIn (juce::Rectangle<float> r, float db)    { return juce::jmap (db, minDb, 0.0f, r.getX(), r.getRight()); }
    static float yForOut (juce::Rectangle<float> r, float db)   { return juce::jmap (db, minDb, 0.0f, r.getBottom(), r.getY()); }

    float thresholdDb() const   { return juce::Decibels::gainToDecibels (comp.thresholdGain.getCurrentValue()); }

    float outputFor (float in) const
    {
        const float t = thresholdDb();
        return in <= t ? in : t + (in - t) * comp.ratio.getCurrentValue();
    }

    juce::Point<float> handlePos() const
    {
        auto r = square();
        const float t = thresholdDb();
        return { xForIn (r, t), juce::jlimit (r.getY(), r.getBottom(), yForOut (r, t + comp.outputDb.getCurrentValue())) };
    }

    te::CompressorPlugin& comp;
    bool dragging = false;
    double lastFingerprint = 0.0;
    ParamGesture gesture;
};

//==============================================================================
/** Delay: le ripetizioni nel tempo, sempre piu' deboli secondo il feedback. */
class DelayVisualiser final : public DeviceVisualiser
{
public:
    DelayVisualiser (te::DelayPlugin& p, juce::Colour c) : DeviceVisualiser (c), delay (p) {}

    void refresh() override
    {
        const double h = fingerprint (delay.feedbackDb->getCurrentValue(), delay.mixProportion->getCurrentValue(), (float) delay.lengthMs.get());

        if (! juce::approximatelyEqual (h, lastFingerprint))
        {
            lastFingerprint = h;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBackground (g);
        auto r = plotArea().withTrimmedBottom (12.0f);

        const float lengthMs = (float) juce::jmax (1, delay.lengthMs.get());
        const float windowMs = juce::jmax (1000.0f, lengthMs * 6.0f);
        const float fb = juce::Decibels::decibelsToGain (delay.feedbackDb->getCurrentValue(), te::DelayPlugin::getMinDelayFeedbackDb());
        const float mix = delay.mixProportion->getCurrentValue();

        auto xFor = [&] (float ms) { return r.getX() + r.getWidth() * ms / windowMs; };

        // Il segnale originale, poi le ripetizioni
        auto drawTap = [&] (float ms, float level, juce::Colour c)
        {
            const float h = r.getHeight() * juce::jlimit (0.0f, 1.0f, level);
            auto bar = juce::Rectangle<float> (xFor (ms) - 2.0f, r.getBottom() - h, 4.0f, h);
            g.setColour (c);
            g.fillRoundedRectangle (bar, 2.0f);
        };

        drawTap (0.0f, 1.0f - mix * 0.5f, colours::textMid);

        float level = mix;

        for (float ms = lengthMs; ms < windowMs && level > 0.01f; ms += lengthMs)
        {
            drawTap (ms, level, colour.withAlpha (0.35f + 0.65f * level));
            level *= fb;
        }

        g.setColour (colours::textDim);
        g.setFont (fonts::mono (9.5f));
        g.drawText (juce::String ((int) lengthMs) + " ms", plotArea().removeFromBottom (11.0f), juce::Justification::centredLeft);
        g.drawText (juce::String (juce::roundToInt (windowMs)) + " ms", plotArea().removeFromBottom (11.0f), juce::Justification::centredRight);
    }

private:
    te::DelayPlugin& delay;
    double lastFingerprint = 0.0;
};

//==============================================================================
/** Riverbero: la coda che si spegne, lunga quanto la stanza e piu' scura con lo smorzamento. */
class ReverbVisualiser final : public DeviceVisualiser
{
public:
    ReverbVisualiser (te::ReverbPlugin& p, juce::Colour c) : DeviceVisualiser (c), reverb (p) {}

    void refresh() override
    {
        const double h = fingerprint (reverb.getRoomSize(), reverb.getDamp(), reverb.getWet());

        if (! juce::approximatelyEqual (h, lastFingerprint))
        {
            lastFingerprint = h;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBackground (g);
        auto r = plotArea();

        const float room = juce::jlimit (0.0f, 1.0f, reverb.getRoomSize());
        const float damp = juce::jlimit (0.0f, 1.0f, reverb.getDamp());
        const float wet = juce::jlimit (0.05f, 1.0f, reverb.getWet() * 3.0f);
        const float decay = 0.08f + room * 0.85f;   // frazione della larghezza in cui la coda si spegne

        juce::Random rnd (1234);
        juce::Path tail;
        tail.startNewSubPath (r.getX(), r.getCentreY());

        // Rumore che si spegne: sopra e sotto la linea centrale
        for (float x = 0.0f; x <= r.getWidth(); x += 1.5f)
        {
            const float t = x / r.getWidth();
            const float env = wet * std::exp (-t / (decay * 0.35f)) * (t < 0.03f ? t / 0.03f : 1.0f);
            const float n = rnd.nextFloat() * 2.0f - 1.0f;
            tail.lineTo (r.getX() + x, r.getCentreY() + n * env * r.getHeight() * 0.48f);
        }

        g.setColour (colour.interpolatedWith (colours::textDim, damp * 0.6f).withAlpha (0.85f));
        g.strokePath (tail, juce::PathStrokeType (1.0f));

        g.setColour (colours::textDim);
        g.setFont (fonts::ui (9.5f, fonts::Weight::medium));
        g.drawText ("Stanza " + juce::String (juce::roundToInt (room * 100.0f)) + "%", r.withHeight (12.0f), juce::Justification::topRight);
    }

private:
    te::ReverbPlugin& reverb;
    double lastFingerprint = 0.0;
};

//==============================================================================
/** LFO che si muove davvero alla velocita' del chorus o del phaser. */
class LfoVisualiser final : public DeviceVisualiser
{
public:
    LfoVisualiser (juce::Colour c, std::function<float()> rateHzIn, std::function<float()> depthIn)
        : DeviceVisualiser (c), rateHz (std::move (rateHzIn)), depth (std::move (depthIn))
    {
    }

    void refresh() override
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        phase += (float) ((now - lastTime) / 1000.0) * juce::jlimit (0.01f, 20.0f, rateHz());
        phase -= std::floor (phase);
        lastTime = now;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        paintBackground (g);
        auto r = plotArea().reduced (0.0f, 4.0f);
        const float amp = juce::jlimit (0.1f, 1.0f, depth()) * r.getHeight() * 0.45f;
        constexpr float cycles = 2.0f;

        g.setColour (colours::divider.brighter (0.15f));
        g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

        juce::Path wave;

        for (float x = 0.0f; x <= r.getWidth(); x += 1.0f)
        {
            const float t = x / r.getWidth() * cycles - phase;
            const float y = r.getCentreY() - std::sin (t * juce::MathConstants<float>::twoPi) * amp;
            wave.isEmpty() ? wave.startNewSubPath (r.getX() + x, y) : wave.lineTo (r.getX() + x, y);
        }

        g.setColour (colour);
        g.strokePath (wave, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Punto che segna il valore attuale, a sinistra
        const float y = r.getCentreY() - std::sin (-phase * juce::MathConstants<float>::twoPi) * amp;
        drawHandle (g, { r.getX() + 4.0f, y }, colour, true, {});

        g.setColour (colours::textDim);
        g.setFont (fonts::mono (9.5f));
        g.drawText (juce::String (rateHz(), 2) + " Hz", plotArea().withHeight (12.0f), juce::Justification::topRight);
    }

private:
    std::function<float()> rateHz, depth;
    float phase = 0.0f;
    double lastTime = juce::Time::getMillisecondCounterHiRes();
};

//==============================================================================
/** Inviluppo del volume del synth (attacco, decadimento, sostegno, rilascio). */
class EnvelopeVisualiser final : public DeviceVisualiser
{
public:
    EnvelopeVisualiser (te::FourOscPlugin& p, juce::Colour c) : DeviceVisualiser (c), synth (p) {}

    void refresh() override
    {
        const double h = fingerprint (synth.ampAttack->getCurrentValue(), synth.ampDecay->getCurrentValue(),
                                      synth.ampSustain->getCurrentValue(), synth.ampRelease->getCurrentValue());

        if (! juce::approximatelyEqual (h, lastFingerprint))
        {
            lastFingerprint = h;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        paintBackground (g);
        auto r = plotArea().reduced (2.0f, 4.0f);

        // Tempi su scala compressa, cosi' anche quelli lunghi stanno nel riquadro
        auto squash = [] (float seconds) { return std::sqrt (juce::jmax (0.0f, seconds)); };
        const float a = squash (synth.ampAttack->getCurrentValue());
        const float d = squash (synth.ampDecay->getCurrentValue());
        const float rel = squash (synth.ampRelease->getCurrentValue());
        const float hold = 0.6f;
        const float total = juce::jmax (0.001f, a + d + hold + rel);
        const float sustain = juce::jlimit (0.0f, 1.0f, synth.ampSustain->getCurrentValue() / 100.0f);

        auto px = [&] (float t) { return r.getX() + r.getWidth() * t / total; };
        auto py = [&] (float level) { return r.getBottom() - r.getHeight() * level; };

        juce::Path env;
        env.startNewSubPath (r.getX(), r.getBottom());
        env.lineTo (px (a), r.getY());
        env.quadraticTo (px (a + d * 0.3f), py (sustain), px (a + d), py (sustain));
        env.lineTo (px (a + d + hold), py (sustain));
        env.quadraticTo (px (a + d + hold + rel * 0.3f), r.getBottom(), px (total), r.getBottom());

        auto fill = env;
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.3f), 0.0f, r.getY(), colour.withAlpha (0.03f), 0.0f, r.getBottom(), false));
        g.fillPath (fill);
        g.setColour (colour);
        g.strokePath (env, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (colours::textDim);
        g.setFont (fonts::mono (9.0f));
        auto labels = plotArea().removeFromTop (11.0f);
        g.drawText ("A D S R", labels, juce::Justification::topRight);
    }

private:
    te::FourOscPlugin& synth;
    double lastFingerprint = 0.0;
};

//==============================================================================
std::unique_ptr<DeviceVisualiser> DeviceVisualiser::create (te::Plugin& p, juce::Colour c)
{
    if (auto eq = dynamic_cast<te::EqualiserPlugin*> (&p))
        return std::make_unique<EqualiserVisualiser> (*eq, c);

    if (auto f = dynamic_cast<te::LowPassPlugin*> (&p))
        return std::make_unique<FilterVisualiser> (*f, c);

    if (auto comp = dynamic_cast<te::CompressorPlugin*> (&p))
        return std::make_unique<CompressorVisualiser> (*comp, c);

    if (auto d = dynamic_cast<te::DelayPlugin*> (&p))
        return std::make_unique<DelayVisualiser> (*d, c);

    if (auto rv = dynamic_cast<te::ReverbPlugin*> (&p))
        return std::make_unique<ReverbVisualiser> (*rv, c);

    if (auto ch = dynamic_cast<te::ChorusPlugin*> (&p))
    {
        // La scheda tiene in vita il device finche' esiste il disegno
        return std::make_unique<LfoVisualiser> (c, [ch] { return ch->speedHz.get(); },
                                                [ch] { return ch->depthMs.get() / 10.0f; });
    }

    if (auto ph = dynamic_cast<te::PhaserPlugin*> (&p))
    {
        return std::make_unique<LfoVisualiser> (c, [ph] { return ph->rate.get(); },
                                                [ph] { return ph->depth.get() / 8.0f; });
    }

    if (auto synth = dynamic_cast<te::FourOscPlugin*> (&p))
        return std::make_unique<EnvelopeVisualiser> (*synth, c);

    return {};
}

} // namespace sonora
