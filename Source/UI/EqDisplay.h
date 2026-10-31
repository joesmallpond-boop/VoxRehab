#pragma once
#include "Widgets.h"
#include "../PluginProcessor.h"

namespace ui
{
inline const juce::Colour bandColour (int b)
{
    static const juce::Colour c[8] { juce::Colour (0xffff6b6b), juce::Colour (0xffffa94d), juce::Colour (0xffffd43b),
                                     juce::Colour (0xff8ce99a), juce::Colour (0xff38d9a9), juce::Colour (0xff4dabf7),
                                     juce::Colour (0xff9775fa), juce::Colour (0xfff783ac) };
    return c[b & 7];
}

//==============================================================================
class Analyser
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kPoints = 220;
    Analyser() : fft (kOrder), window (kN, juce::dsp::WindowingFunction<float>::hann)
    {
        db.fill (-120.0f);
        fftData.resize (2 * kN);
    }
    void update (const ScopeBuffer& sb, double fs)
    {
        sb.copyLatest (fftData.data(), kN);
        std::fill (fftData.begin() + kN, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), kN);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);
        for (int p = 0; p < kPoints; ++p)
        {
            const double f = 20.0 * std::pow (1000.0, p / (double) (kPoints - 1));
            const double bin = f * kN / fs;
            const int k0 = (int) bin;
            const int k1 = juce::jmax (k0 + 1, (int) (20.0 * std::pow (1000.0, (p + 1) / (double) (kPoints - 1)) * kN / fs));
            float m = 0;
            for (int k = k0; k <= juce::jmin (k1, kN / 2 - 1); ++k) m = juce::jmax (m, fftData[(size_t) k]);
            // tilt +3 dB/oct so a voice reads flat-ish, normalise for window
            const float v = juce::Decibels::gainToDecibels (m * 4.0f / kN, -120.0f) + 3.0f * (float) std::log2 (f / 1000.0);
            db[(size_t) p] = v > db[(size_t) p] ? v : db[(size_t) p] * 0.85f + v * 0.15f;
        }
    }
    std::array<float, kPoints> db;

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> fftData;
};

//==============================================================================
class EqDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    EqDisplay (VoxRehabProcessor& p) : proc (p)
    {
        setTooltip ("Drag nodes: freq/gain  •  Wheel: Q  •  Double-click node: band on/off  •  "
                    "Double-click empty space: add band  •  Right-click node: dynamic on/off  •  Alt-click: reset gain");
    }

    std::function<void (int)> onSelect;
    int selected = 0;

    void tick()
    {
        const double fs = proc.getCurrentSampleRate();
        inA.update (proc.scopeIn, fs);
        outA.update (proc.scopeOut, fs);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg.darker (0.3f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        plot = r.reduced (8.0f, 6.0f).withTrimmedTop (26.0f).withTrimmedBottom (12.0f);

        // grid
        g.setFont (font (10.0f));
        for (float f : { 30.f, 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 20000.f })
        {
            const float x = xFor (f);
            g.setColour (col::border.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (col::dim);
            const juce::String t = f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f);
            g.drawText (t, juce::Rectangle<float> (x - 20, plot.getBottom() + 1, 40, 12), juce::Justification::centred);
        }
        for (int d = -18; d <= 18; d += 6)
        {
            const float y = yFor ((float) d);
            g.setColour (d == 0 ? col::border.brighter (0.4f) : col::border.withAlpha (0.6f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (col::dim);
            g.drawText ((d > 0 ? "+" : "") + juce::String (d), juce::Rectangle<float> (plot.getRight() - 26, y - 7, 24, 12),
                        juce::Justification::centredRight);
        }

        // spectra
        auto specPath = [this] (const Analyser& a, bool close)
        {
            juce::Path p;
            for (int i = 0; i < Analyser::kPoints; ++i)
            {
                const float f = 20.0f * std::pow (1000.0f, i / (float) (Analyser::kPoints - 1));
                const float y = juce::jlimit (plot.getY(), plot.getBottom(), specY (a.db[(size_t) i]));
                if (i == 0) p.startNewSubPath (xFor (f), y); else p.lineTo (xFor (f), y);
            }
            if (close) { p.lineTo (plot.getRight(), plot.getBottom()); p.lineTo (plot.getX(), plot.getBottom()); p.closeSubPath(); }
            return p;
        };
        g.setColour (juce::Colour (0xff8fa3b8).withAlpha (0.35f));
        g.strokePath (specPath (inA, false), juce::PathStrokeType (1.0f));
        g.setColour (juce::Colour (0xff64ffda).withAlpha (0.10f));
        g.fillPath (specPath (outA, true));
        g.setColour (juce::Colour (0xff64ffda).withAlpha (0.45f));
        g.strokePath (specPath (outA, false), juce::PathStrokeType (1.2f));

        // curves
        const double fs = proc.getCurrentSampleRate();
        const bool eqOn = val (ids::eq_on) > 0.5f;
        std::array<vr::SvfCoefs, 8> live, stat;
        std::array<bool, 8> on {};
        for (int b = 0; b < 8; ++b)
        {
            on[(size_t) b] = val (eqId (b, "on")) > 0.5f;
            const int type = (int) val (eqId (b, "type"));
            stat[(size_t) b] = vr::makeSvf (type, fs, val (eqId (b, "freq")), val (eqId (b, "gain")), val (eqId (b, "q")));
            live[(size_t) b] = vr::makeSvf (type, fs, val (eqId (b, "freq")), proc.eq.liveGain[(size_t) b].load(), val (eqId (b, "q")));
        }
        const int W = (int) plot.getWidth();
        for (int b = 0; b < 8; ++b)
        {
            if (! on[(size_t) b]) continue;
            juce::Path p;
            for (int x = 0; x <= W; x += 2)
            {
                const double f = freqFor (plot.getX() + x);
                const float y = yFor ((float) juce::Decibels::gainToDecibels (vr::svfMagnitude (stat[(size_t) b], fs, f), -60.0));
                if (x == 0) p.startNewSubPath (plot.getX() + x, y); else p.lineTo (plot.getX() + x, y);
            }
            g.setColour (bandColour (b).withAlpha (b == selected ? 0.55f : 0.25f));
            g.strokePath (p, juce::PathStrokeType (1.0f));
        }
        auto totalPath = [&] (const std::array<vr::SvfCoefs, 8>& cs)
        {
            juce::Path p;
            for (int x = 0; x <= W; x += 2)
            {
                const double f = freqFor (plot.getX() + x);
                double m = 1.0;
                for (int b = 0; b < 8; ++b) if (on[(size_t) b]) m *= vr::svfMagnitude (cs[(size_t) b], fs, f);
                const float y = juce::jlimit (plot.getY() - 4, plot.getBottom() + 4, yFor ((float) juce::Decibels::gainToDecibels (m, -60.0)));
                if (x == 0) p.startNewSubPath (plot.getX() + x, y); else p.lineTo (plot.getX() + x, y);
            }
            return p;
        };
        g.setColour (juce::Colours::white.withAlpha (eqOn ? 0.35f : 0.15f));
        g.strokePath (totalPath (stat), juce::PathStrokeType (1.0f, juce::PathStrokeType::curved), {});
        g.setColour (eqOn ? juce::Colour (0xffe9fffa) : col::dim);
        g.strokePath (totalPath (live), juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // nodes
        for (int b = 0; b < 8; ++b)
        {
            const auto pt = nodePos (b);
            const auto c = bandColour (b);
            const bool dyn = val (eqId (b, "dyn")) > 0.5f;
            if (on[(size_t) b] && dyn)
            {
                const float y2 = yFor (juce::jlimit (-24.0f, 24.0f, val (eqId (b, "gain")) + val (eqId (b, "range"))));
                g.setColour (c.withAlpha (0.25f));
                g.fillRoundedRectangle (juce::Rectangle<float> (pt.x - 3, juce::jmin (pt.y, y2), 6, std::abs (y2 - pt.y)), 3.0f);
                const float yl = yFor (juce::jlimit (-24.0f, 24.0f, proc.eq.liveGain[(size_t) b].load()));
                g.setColour (c);
                g.drawEllipse (pt.x - 4, yl - 4, 8, 8, 1.5f);
            }
            const float rad = b == selected ? 9.0f : 7.5f;
            g.setColour (on[(size_t) b] ? c : c.withAlpha (0.25f));
            g.fillEllipse (pt.x - rad, pt.y - rad, rad * 2, rad * 2);
            if (b == selected) { g.setColour (juce::Colours::white); g.drawEllipse (pt.x - rad - 2, pt.y - rad - 2, rad * 2 + 4, rad * 2 + 4, 1.5f); }
            g.setColour (juce::Colours::black.withAlpha (on[(size_t) b] ? 0.85f : 0.5f));
            g.setFont (font (10.5f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (pt.x - rad, pt.y - rad, rad * 2, rad * 2), juce::Justification::centred);
            if (dyn) { g.setColour (c); g.setFont (font (9.0f, true)); g.drawText ("D", juce::Rectangle<float> (pt.x + rad - 2, pt.y - rad - 10, 12, 10), juce::Justification::centred); }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = hitNode (e.position);
        if (drag < 0) return;
        select (drag);
        if (e.mods.isPopupMenu()) { toggle (eqId (drag, "dyn")); drag = -1; return; }
        if (e.mods.isAltDown()) { set (eqId (drag, "gain"), 0.0f); drag = -1; return; }
        for (auto* f : { "freq", "gain" }) if (auto* p = param (eqId (drag, f))) p->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag < 0) return;
        const float x = juce::jlimit (plot.getX(), plot.getRight(), e.position.x);
        const float y = juce::jlimit (plot.getY(), plot.getBottom(), e.position.y);
        set (eqId (drag, "freq"), (float) freqFor (x), false);
        const int type = (int) val (eqId (drag, "type"));
        if (type <= vr::HighShelf) set (eqId (drag, "gain"), gainFor (y), false);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag >= 0) for (auto* f : { "freq", "gain" }) if (auto* p = param (eqId (drag, f))) p->endChangeGesture();
        drag = -1;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = hitNode (e.position);
        if (b >= 0) { toggle (eqId (b, "on")); return; }
        for (int i = 0; i < 8; ++i)
            if (val (eqId (i, "on")) < 0.5f)
            {
                set (eqId (i, "type"), 0.0f); set (eqId (i, "freq"), (float) freqFor (e.position.x));
                set (eqId (i, "gain"), gainFor (e.position.y)); set (eqId (i, "on"), 1.0f);
                select (i);
                return;
            }
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        int b = hitNode (e.position);
        if (b < 0) b = selected;
        const float q = val (eqId (b, "q")) * std::pow (1.25f, w.deltaY * 4.0f);
        set (eqId (b, "q"), juce::jlimit (0.1f, 18.0f, q));
    }

private:
    float val (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }
    juce::RangedAudioParameter* param (const juce::String& id) { return proc.apvts.getParameter (id); }
    void set (const juce::String& id, float v, bool gesture = true)
    {
        if (auto* p = param (id))
        {
            if (gesture) p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            if (gesture) p->endChangeGesture();
        }
    }
    void toggle (const juce::String& id) { set (id, val (id) > 0.5f ? 0.0f : 1.0f); }
    void select (int b) { selected = b; if (onSelect) onSelect (b); repaint(); }

    float xFor (float f) const { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); }
    double freqFor (float x) const { return 20.0 * std::pow (1000.0, (x - plot.getX()) / plot.getWidth()); }
    float yFor (float db) const { return plot.getCentreY() - db / 24.0f * plot.getHeight() * 0.5f; }
    float gainFor (float y) const { return juce::jlimit (-24.0f, 24.0f, (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * 24.0f); }
    float specY (float db) const { return plot.getY() + (-6.0f - db) / 84.0f * plot.getHeight(); }

    juce::Point<float> nodePos (int b) const
    {
        const int type = (int) val (eqId (b, "type"));
        const float gdb = type <= vr::HighShelf ? val (eqId (b, "gain")) : 0.0f;
        return { xFor (val (eqId (b, "freq"))), yFor (gdb) };
    }
    int hitNode (juce::Point<float> p) const
    {
        int best = -1; float bd = 14.0f;
        for (int b = 0; b < 8; ++b)
        {
            const float d = nodePos (b).getDistanceFrom (p);
            if (d < bd) { bd = d; best = b; }
        }
        return best;
    }

    VoxRehabProcessor& proc;
    Analyser inA, outA;
    juce::Rectangle<float> plot;
    int drag = -1;
};

//==============================================================================
// Knobs for the currently selected band
class BandEditor : public juce::Component
{
public:
    explicit BandEditor (APVTS& s) : state (s)
    {
        for (int b = 0; b < 8; ++b)
        {
            auto* bt = selectors.add (new juce::TextButton (juce::String (b + 1)));
            bt->setRadioGroupId (77);
            bt->setClickingTogglesState (true);
            bt->setColour (juce::TextButton::buttonOnColourId, bandColour (b));
            bt->onClick = [this, b] { if (onSelect) onSelect (b); };
            addAndMakeVisible (bt);
        }
        const char* names[8] { "Freq", "Gain", "Q", "Thresh", "Ratio", "Range", "Attack", "Release" };
        for (auto* n : names) addAndMakeVisible (knobs.add (new Knob (n, bandColour (0))));
        for (int i = 0; i < 6; ++i) typeBox.addItem (eqTypeNames()[i], i + 1);
        addAndMakeVisible (typeBox);
        onBtn.setButtonText ("BAND ON"); onBtn.setClickingTogglesState (true);
        dynBtn.setButtonText ("DYNAMIC"); dynBtn.setClickingTogglesState (true);
        eqOn.setButtonText ("EQ ON"); eqOn.setClickingTogglesState (true);
        eqOn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff64ffda));
        eqOnAtt = std::make_unique<APVTS::ButtonAttachment> (s, ids::eq_on, eqOn);
        for (auto* b : { &onBtn, &dynBtn, &eqOn }) addAndMakeVisible (b);
        setBand (0);
    }

    std::function<void (int)> onSelect;

    void setBand (int b)
    {
        band = b;
        const auto c = bandColour (b);
        selectors[b]->setToggleState (true, juce::dontSendNotification);
        const char* fields[8] { "freq", "gain", "q", "thresh", "ratio", "range", "attack", "release" };
        for (int i = 0; i < 8; ++i) { knobs[i]->attach (state, eqId (b, fields[i])); knobs[i]->setAccent (c); }
        typeAtt.reset(); onAtt.reset(); dynAtt.reset();
        typeAtt = std::make_unique<APVTS::ComboBoxAttachment> (state, eqId (b, "type"), typeBox);
        onAtt = std::make_unique<APVTS::ButtonAttachment> (state, eqId (b, "on"), onBtn);
        dynAtt = std::make_unique<APVTS::ButtonAttachment> (state, eqId (b, "dyn"), dynBtn);
        onBtn.setColour (juce::TextButton::buttonOnColourId, c);
        dynBtn.setColour (juce::TextButton::buttonOnColourId, c);
        repaint();
    }

    void refreshDynEnabled()
    {
        const bool dyn = dynBtn.getToggleState();
        for (int i = 3; i < 8; ++i) knobs[i]->setAlpha (dyn ? 1.0f : 0.4f);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (juce::Colour (0xff64ffda));
        g.setFont (font (12.0f, true));
        g.drawText ("8-BAND DYNAMIC EQ", getLocalBounds().removeFromTop (26).reduced (10, 0), juce::Justification::centredLeft);
        g.setColour (col::dim);
        g.setFont (font (10.0f, true));
        g.drawText ("DYNAMICS", dynLabel, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 4);
        auto head = r.removeFromTop (22);
        eqOn.setBounds (head.removeFromRight (64).reduced (0, 1));
        r.removeFromTop (4);
        auto sel = r.removeFromTop (24);
        const int bw = sel.getWidth() / 8;
        for (auto* b : selectors) b->setBounds (sel.removeFromLeft (bw).reduced (2, 0));
        r.removeFromTop (6);
        auto ctl = r.removeFromTop (24);
        typeBox.setBounds (ctl.removeFromLeft (ctl.getWidth() / 3).reduced (2, 0));
        onBtn.setBounds (ctl.removeFromLeft (ctl.getWidth() / 2).reduced (2, 0));
        dynBtn.setBounds (ctl.reduced (2, 0));
        r.removeFromTop (4);
        auto row1 = r.removeFromTop (r.getHeight() / 2);
        const int kw = row1.getWidth() / 3;
        for (int i = 0; i < 3; ++i) knobs[i]->setBounds (row1.removeFromLeft (kw).reduced (4, 2));
        dynLabel = r.removeFromTop (12);
        const int kw2 = r.getWidth() / 5;
        for (int i = 3; i < 8; ++i) knobs[i]->setBounds (r.removeFromLeft (kw2).reduced (2, 0));
    }

private:
    APVTS& state;
    int band = 0;
    juce::OwnedArray<juce::TextButton> selectors;
    juce::OwnedArray<Knob> knobs;
    juce::ComboBox typeBox;
    juce::TextButton onBtn, dynBtn, eqOn;
    juce::Rectangle<int> dynLabel;
    std::unique_ptr<APVTS::ComboBoxAttachment> typeAtt;
    std::unique_ptr<APVTS::ButtonAttachment> onAtt, dynAtt, eqOnAtt;
};
} // namespace ui
