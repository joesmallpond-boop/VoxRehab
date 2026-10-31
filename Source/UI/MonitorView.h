#pragma once
// Process Monitor: real-time view of what one module is doing to the audio.
//  • left: scrolling history — level in (grey) vs out (colour), the removed part shaded red,
//          the module's detector / sidechain level against its threshold, a gain-reduction
//          lane and markers where the module triggered
//  • right: "reduction by frequency" — where in the spectrum it is acting right now
#include "Widgets.h"
#include "EqDisplay.h"

namespace ui
{
struct ModuleInfo
{
    const char* name;
    juce::Colour colour;
    const char* sideLabel;     // detector trace legend (nullptr = none)
    const char* help;
};

inline const ModuleInfo& moduleInfo (int m)
{
    static const ModuleInfo info[Monitor::kNum] = {
        { "Overall (whole chain)", juce::Colour (0xffe6e6e6), nullptr,
          "Everything the plugin is doing: input vs. final output, time-aligned. Red = level removed. Right: output spectrum minus input spectrum (includes EQ)." },
        { "De-Hum", juce::Colour (0xffe0b84a), nullptr,
          "Energy removed by the hum notches. Right: the notch comb (fundamental + harmonics)." },
        { "De-Click", juce::Colour (0xfff08a5d), nullptr,
          "Markers = each click found. GR lane = how far the click band is dipped. Right: the repair shape at the last click." },
        { "De-Plosive", juce::Colour (0xffff6b6b), "low band (pop detector)",
          "White = pop-band level (below Freq, or the selected band in Band mode), dashed = voice body. When it jumps above the body the band ducks - note it starts just before the pop (lookahead). Markers = pops caught." },
        { "De-Noise", juce::Colour (0xff4fc3f7), nullptr,
          "GR lane = average noise reduction across the voice band. Right: reduction at each frequency right now (deepest where there is only noise)." },
        { "Background Remover", juce::Colour (0xff26c6da), nullptr,
          "GR lane = gate closing between phrases + spill pushed down under the voice. Right: reduction by frequency." },
        { "Voice Isolator", juce::Colour (0xffab8cff), nullptr,
          "GR lane = energy removed between the voice harmonics and outside the voice range. Right: the current mask (harmonic comb when voiced)." },
        { "Breath", juce::Colour (0xff9ccc65), nullptr,
          "Markers = breaths detected. GR lane = how much each breath is turned down." },
        { "De-Verb", juce::Colour (0xff7986cb), nullptr,
          "GR lane = room tail removed. Watch phrase ends: the red area is the reverb tail being cut. Right: reduction by frequency." },
        { "De-Esser", juce::Colour (0xffffd54f), "sibilance detector",
          "White = sibilant band level, dashed line = threshold. Every time it crosses, the GR lane shows the cut. Right: the split-band (high shelf) cut right now." },
        { "Comp 1 - FET", juce::Colour (0xffff8a65), "detector",
          "Detector level vs threshold (dashed). GR lane shows attack grabbing peaks and the release recovering." },
        { "Comp 2 - Opto", juce::Colour (0xffffb74d), "light cell drive",
          "Cell drive vs threshold (dashed). Note the slow, programme-dependent release in the GR lane." },
        { "Dynamic EQ", juce::Colour (0xff64ffda), nullptr,
          "GR lane = largest dynamic band movement. Right: the live EQ curve including dynamic bands." },
        { "Glue / Console", juce::Colour (0xffd4a373), "glue detector",
          "GR lane = bus 'glue' compression (ballistics depend on the character). Red = level held back. Right: the console / tape tone curve; Drive adds harmonics on top." },
    };
    return info[juce::jlimit (0, Monitor::kNum - 1, m)];
}

class MonitorView : public juce::Component
{
public:
    explicit MonitorView (VoxRehabProcessor& p) : proc (p) {}

    void setModule (int m) { module = m; grHold = 0; repaint(); }
    int getModule() const { return module; }
    void setSpanSeconds (float s) { spanSeconds = s; repaint(); }

    void tick()
    {
        if (module == Monitor::Overall)
        {
            const double fs = proc.getCurrentSampleRate();
            inA.update (proc.scopeIn, fs);
            outA.update (proc.scopeOut, fs);
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& mi = moduleInfo (module);
        const auto acc = mi.colour;
        auto r = getLocalBounds().toFloat();
        g.setColour (col::bg.darker (0.3f));
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col::border);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);

        auto area = getLocalBounds().reduced (10, 8);
        area.removeFromTop (28);                         // space for the selector + header text
        auto right = area.removeFromRight (290);
        area.removeFromRight (12);
        auto time = area;

        // ----- gather history
        const auto& tr = proc.monitor.tr[(size_t) module];
        const int now = proc.monitor.tick.load (std::memory_order_acquire) - 1;
        const float tickSec = juce::jmax (1.0e-4f, proc.monitor.tickSeconds.load());
        const int spanTicks = juce::jlimit (32, Monitor::kLen - 64, (int) (spanSeconds / tickSec));
        const int lat = juce::roundToInt (tr.latencyTicks.load());
        const int W = juce::jmax (1, time.getWidth());
        cols.resize ((size_t) W);
        float grNow = 0, grPeak = 0; int events = 0;
        for (int x = 0; x < W; ++x)
        {
            const int t0 = now - spanTicks + (int) ((int64_t) x * spanTicks / W);
            const int t1 = juce::jmax (t0 + 1, now - spanTicks + (int) ((int64_t) (x + 1) * spanTicks / W));
            Col c;
            for (int t = t0; t < t1; ++t)
            {
                if (t < 0) continue;
                const int i = t & (Monitor::kLen - 1);
                const int ip = (t - lat) & (Monitor::kLen - 1);
                const bool havePre = t - lat >= 0;
                const float pre = havePre ? tr.pre[(size_t) ip] : -120.0f, post = tr.post[(size_t) i];
                float grv = tr.gr[(size_t) i];
                if (module == Monitor::Overall) grv = havePre ? juce::jmax (0.0f, pre - post) : 0.0f;
                c.pre = juce::jmax (c.pre, pre);
                c.post = juce::jmax (c.post, post);
                c.gr = juce::jmax (c.gr, grv);
                c.side = juce::jmax (c.side, tr.side[(size_t) i]);
                c.side2 = juce::jmax (c.side2, tr.side2[(size_t) i]);
                c.ev = c.ev || tr.ev[(size_t) i] != 0;
                c.valid = true;
            }
            cols[(size_t) x] = c;
            grPeak = juce::jmax (grPeak, c.gr);
            if (c.ev) ++events;
        }
        for (int x = W - 1; x >= juce::jmax (0, W - 4); --x) grNow = juce::jmax (grNow, cols[(size_t) x].gr);
        grHold = grNow > grHold ? grNow : grHold * 0.93f;

        // ----- header readouts
        g.setFont (font (12.0f, true));
        g.setColour (acc);
        g.drawText ("MONITOR: " + juce::String (mi.name).toUpperCase(),
                    juce::Rectangle<int> (232, 8, 300, 22), juce::Justification::centredLeft);
        g.setFont (font (11.0f));
        g.setColour (col::text);
        juce::String ro = "now " + juce::String (grNow, 1) + " dB   peak " + juce::String (grPeak, 1) + " dB";
        if (module == Monitor::Click || module == Monitor::Plosive || module == Monitor::Breath)
            ro << "   " << events << (module == Monitor::Click ? " clicks" : module == Monitor::Plosive ? " pops" : " breaths")
               << " in view";
        if (module == Monitor::Isolator)
        {
            const float f0 = tr.side[(size_t) (now & (Monitor::kLen - 1))];
            ro << "   pitch " << (f0 > 0 ? juce::String (juce::roundToInt (f0)) + " Hz" : juce::String ("--"));
        }
        g.drawText (ro, juce::Rectangle<int> (time.getX() + 520, 8, time.getWidth() - 520, 22), juce::Justification::centredRight);

        // ----- lanes
        auto timeF = time.toFloat();
        auto grLane = timeF.removeFromTop (timeF.getHeight() * 0.30f);
        timeF.removeFromTop (6);
        auto lvLane = timeF.withTrimmedBottom (14);
        const float grRange = grRangeFor (grPeak);

        // GR lane
        g.setColour (col::panel);
        g.fillRect (grLane);
        g.setFont (font (9.5f));
        for (float d = 0; d <= grRange; d += grRange / 3)
        {
            const float y = grLane.getY() + d / grRange * grLane.getHeight();
            g.setColour (col::border.withAlpha (0.7f));
            g.drawHorizontalLine ((int) y, grLane.getX(), grLane.getRight());
            g.setColour (col::dim);
            g.drawText ("-" + juce::String ((int) d), juce::Rectangle<float> (grLane.getX() + 2, y - (d > 0 ? 11 : 0), 28, 11), juce::Justification::left);
        }
        {
            juce::Path p;
            p.startNewSubPath (grLane.getX(), grLane.getY());
            for (int x = 0; x < W; ++x)
                p.lineTo (grLane.getX() + (float) x, grLane.getY() + juce::jmin (1.0f, cols[(size_t) x].gr / grRange) * grLane.getHeight());
            p.lineTo (grLane.getRight(), grLane.getY());
            p.closeSubPath();
            g.setColour (acc.withAlpha (0.35f));
            g.fillPath (p);
            g.setColour (acc);
            g.strokePath (p, juce::PathStrokeType (1.2f));
        }
        g.setColour (col::dim);
        g.drawText ("GAIN REDUCTION", grLane.reduced (4, 2), juce::Justification::topRight);

        // level lane
        auto yLv = [&] (float db) { return lvLane.getY() + juce::jlimit (0.0f, 1.0f, -db / 66.0f) * lvLane.getHeight(); };
        g.setColour (col::panel.withAlpha (0.6f));
        g.fillRect (lvLane);
        for (int d = 0; d >= -60; d -= 12)
        {
            g.setColour (col::border.withAlpha (0.7f));
            g.drawHorizontalLine ((int) yLv ((float) d), lvLane.getX(), lvLane.getRight());
            g.setColour (col::dim);
            g.drawText (juce::String (d), juce::Rectangle<float> (lvLane.getX() + 2, yLv ((float) d) - 11, 28, 11), juce::Justification::left);
        }
        const float bottom = lvLane.getBottom();
        for (int x = 0; x < W; ++x)
        {
            const auto& c = cols[(size_t) x];
            if (! c.valid) continue;
            const float xx = lvLane.getX() + (float) x;
            const float yPre = yLv (c.pre), yPost = yLv (c.post);
            g.setColour (acc.withAlpha (0.22f));
            g.drawVerticalLine ((int) xx, yPost, bottom);
            if (yPre < yPost - 0.5f)
            {
                g.setColour (removedColour().withAlpha (0.55f));
                g.drawVerticalLine ((int) xx, yPre, yPost);
            }
        }
        auto trace = [&] (auto getter, juce::Colour c, float w, bool dashed)
        {
            juce::Path p; bool started = false;
            for (int x = 0; x < W; ++x)
            {
                const auto& cc = cols[(size_t) x];
                const float v = getter (cc);
                if (! cc.valid || v < -100.0f) { started = false; continue; }
                const float px = lvLane.getX() + (float) x, py = yLv (v);
                if (! started) { p.startNewSubPath (px, py); started = true; } else p.lineTo (px, py);
            }
            g.setColour (c);
            if (dashed)
            {
                juce::Path d; const float dl[] { 4.0f, 3.0f };
                juce::PathStrokeType (w).createDashedStroke (d, p, dl, 2);
                g.fillPath (d);
            }
            else g.strokePath (p, juce::PathStrokeType (w));
        };
        trace ([] (const Col& c) { return c.pre; }, col::dim.withAlpha (0.8f), 1.0f, false);
        trace ([] (const Col& c) { return c.post; }, acc, 1.6f, false);

        // detector / sidechain + threshold
        const float thr = thresholdFor (module, now);
        if (mi.sideLabel != nullptr)
        {
            const auto sc = juce::Colours::white;
            trace ([] (const Col& c) { return c.side; }, sc, 1.4f, false);
            if (module == Monitor::Plosive)
                trace ([] (const Col& c) { return c.side2; }, col::text.withAlpha (0.6f), 1.0f, true);
            if (thr > -100.0f)
            {
                juce::Path tl; tl.startNewSubPath (lvLane.getX(), yLv (thr)); tl.lineTo (lvLane.getRight(), yLv (thr));
                juce::Path d; const float dl[] { 6.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (d, tl, dl, 2);
                g.setColour (sc.withAlpha (0.9f));
                g.fillPath (d);
                g.setFont (font (10.0f, true));
                g.drawText ("threshold " + juce::String (thr, 1), juce::Rectangle<float> (lvLane.getRight() - 120, yLv (thr) - 13, 116, 12), juce::Justification::right);
            }
        }

        // events
        for (int x = 0; x < W; ++x)
            if (cols[(size_t) x].ev)
            {
                const float xx = lvLane.getX() + (float) x;
                g.setColour (juce::Colours::white.withAlpha (0.18f));
                g.drawVerticalLine ((int) xx, grLane.getY(), bottom);
                juce::Path tri;
                tri.addTriangle (xx - 4, grLane.getY(), xx + 4, grLane.getY(), xx, grLane.getY() + 6);
                g.setColour (juce::Colours::white);
                g.fillPath (tri);
            }

        // time axis + legend
        g.setFont (font (10.0f));
        g.setColour (col::dim);
        const float step = spanSeconds <= 2.5f ? 0.5f : (spanSeconds <= 6 ? 1.0f : 2.0f);
        for (float s = 0; s <= spanSeconds + 0.001f; s += step)
        {
            const float xx = lvLane.getRight() - s / spanSeconds * lvLane.getWidth();
            g.drawText (s == 0 ? juce::String ("now") : "-" + juce::String (s, s < 1 ? 1 : 0) + "s",
                        juce::Rectangle<float> (xx - 20, bottom + 1, 40, 12), juce::Justification::centred);
        }
        legend (g, lvLane, acc, mi);

        grPeakView = grPeak;
        paintFrequency (g, right, acc, mi);
    }

private:
    struct Col { float pre = -120, post = -120, gr = 0, side = -120, side2 = -120; bool ev = false, valid = false; };

    juce::Colour removedColour() const { return module == Monitor::Plosive ? juce::Colour (0xff40c4ff) : juce::Colour (0xffff5252); }

    static float grRangeFor (float peak)
    {
        for (float r : { 6.0f, 12.0f, 24.0f, 48.0f }) if (peak <= r * 0.95f) return r;
        return 60.0f;
    }

    float val (const juce::String& id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    float thresholdFor (int m, int now) const
    {
        switch (m)
        {
            case Monitor::DeEss: return val (ids::de_thresh);
            case Monitor::Fet:   return val (ids::c_thresh);
            case Monitor::Glue:  return val (ids::gl_on) > 0.5f && val (ids::gl_glue) > 0.1f ? -4.0f - 0.26f * val (ids::gl_glue) : -1000.0f;
            case Monitor::Opto:  return proc.monitor.tr[(size_t) m].side2[(size_t) (now & (Monitor::kLen - 1))];
            default:             return -1000.0f;
        }
    }

    void legend (juce::Graphics& g, juce::Rectangle<float> lane, juce::Colour acc, const ModuleInfo& mi)
    {
        auto r = juce::Rectangle<float> (lane.getX() + 34, lane.getY() + 4, 520, 14);
        auto item = [&] (juce::Colour c, const juce::String& t)
        {
            g.setColour (c);
            g.fillRect (r.removeFromLeft (10).withSizeKeepingCentre (10, 3));
            r.removeFromLeft (4);
            g.setColour (col::text.withAlpha (0.8f));
            const int w = (int) juce::GlyphArrangement::getStringWidth (font (10.0f), t) + 14;
            g.drawText (t, r.removeFromLeft ((float) w), juce::Justification::centredLeft);
        };
        g.setFont (font (10.0f));
        item (col::dim, "in");
        item (acc, "out");
        item (removedColour().withAlpha (0.8f), "removed");
        if (mi.sideLabel != nullptr)
            item (juce::Colours::white, mi.sideLabel);
        if (module == Monitor::Plosive) item (col::text.withAlpha (0.6f), "voice body (dashed)");
    }

    void paintFrequency (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour acc, const ModuleInfo& mi)
    {
        g.setColour (col::dim);
        g.setFont (font (10.0f, true));
        g.drawText ("REDUCTION BY FREQUENCY", area.removeFromTop (14), juce::Justification::centredLeft);
        auto helpArea = area.removeFromBottom (62);
        auto plot = area.toFloat().reduced (0, 2).withTrimmedBottom (12);
        g.setColour (col::panel);
        g.fillRect (plot);

        const float top = 6.0f, bot = -30.0f;
        auto yFor = [&] (float db) { return plot.getY() + juce::jlimit (0.0f, 1.0f, (top - db) / (top - bot)) * plot.getHeight(); };
        auto xFor = [&] (double f) { return plot.getX() + plot.getWidth() * (float) (std::log (f / 20.0) / std::log (1000.0)); };
        g.setFont (font (9.5f));
        for (float d : { 0.0f, -12.0f, -24.0f })
        {
            g.setColour (d == 0 ? col::border.brighter (0.4f) : col::border);
            g.drawHorizontalLine ((int) yFor (d), plot.getX(), plot.getRight());
            g.setColour (col::dim);
            g.drawText (juce::String ((int) d), juce::Rectangle<float> (plot.getX() + 2, yFor (d) - 11, 26, 11), juce::Justification::left);
        }
        for (double f : { 100.0, 1000.0, 10000.0 })
        {
            g.setColour (col::border);
            g.drawVerticalLine ((int) xFor (f), plot.getY(), plot.getBottom());
            g.setColour (col::dim);
            g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                        juce::Rectangle<float> (xFor (f) - 15, plot.getBottom() + 1, 30, 11), juce::Justification::centred);
        }

        const int P = 160;
        std::vector<float> db ((size_t) P, 0.0f);
        const double fs = proc.getCurrentSampleRate();
        for (int i = 0; i < P; ++i) db[(size_t) i] = responseAt (20.0 * std::pow (1000.0, i / (double) (P - 1)), fs, grHold);

        const bool momentary = module == Monitor::Click || module == Monitor::Plosive || module == Monitor::DeEss
                            || module == Monitor::Fet || module == Monitor::Opto || module == Monitor::Glue;
        if (momentary && grPeakView > 0.05f)
        {
            juce::Path pk;
            for (int i = 0; i < P; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * i / (float) (P - 1);
                const float y = yFor (responseAt (20.0 * std::pow (1000.0, i / (double) (P - 1)), fs, grPeakView));
                if (i == 0) pk.startNewSubPath (x, y); else pk.lineTo (x, y);
            }
            juce::Path d; const float dl[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.2f).createDashedStroke (d, pk, dl, 2);
            g.setColour (acc.withAlpha (0.7f));
            g.fillPath (d);
            g.setFont (font (9.5f));
            g.drawText ("dashed = peak in view", plot.reduced (4, 3), juce::Justification::bottomRight);
        }

        juce::Path line, fill;
        for (int i = 0; i < P; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * i / (float) (P - 1);
            const float y = yFor (db[(size_t) i]);
            if (i == 0) { line.startNewSubPath (x, y); fill.startNewSubPath (x, yFor (0)); }
            else line.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (plot.getRight(), yFor (0));
        fill.closeSubPath();
        g.setColour (acc.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (acc);
        g.strokePath (line, juce::PathStrokeType (1.8f));

        g.setColour (col::dim);
        g.setFont (font (10.0f));
        g.drawFittedText (mi.help, helpArea.reduced (0, 2), juce::Justification::topLeft, 5, 0.9f);
    }

    static std::complex<double> biquadH (const vr::Biquad& b, double f, double fs)
    {
        const std::complex<double> z1 = std::polar (1.0, -2.0 * vr::kPi * f / fs), z2 = z1 * z1;
        return (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
    }

    float responseAt (double f, double fs, float gr)
    {
        using namespace ids;
        const float on = 1.0f;
        switch (module)
        {
            case Monitor::Overall:
            {
                const float pos = (float) (std::log (f / 20.0) / std::log (1000.0)) * (Analyser::kPoints - 1);
                const int i = juce::jlimit (0, Analyser::kPoints - 1, (int) pos);
                const float d = outA.db[(size_t) i] - inA.db[(size_t) i];
                auto& s = overallSmooth[(size_t) i];
                s = 0.9f * s + 0.1f * (inA.db[(size_t) i] > -100.0f ? d : 0.0f);
                return s;
            }
            case Monitor::Hum:
            {
                if (val (hum_on) < 0.5f) return 0.0f;
                const double base = val (hum_freq) > 0.5f ? 60.0 : 50.0, q = val (hum_q);
                const double amt = 1.0 - vr::dbToGain (-val (hum_depth));
                std::complex<double> H (1.0, 0.0);
                for (int h = 0; h < (int) val (hum_harm); ++h)
                {
                    if (base * (h + 1) > 0.45 * fs) break;
                    vr::Biquad b; b.setBandpass (fs, base * (h + 1), q * (h + 1));
                    H *= 1.0 - amt * biquadH (b, f, fs);
                }
                return (float) juce::Decibels::gainToDecibels (std::abs (H), -60.0);
            }
            case Monitor::Click:
            {
                vr::Biquad lp; lp.setLowpass (fs, val (dc_focus), 0.70710678);
                const auto L = biquadH (lp, f, fs);
                const double gg = vr::dbToGain (-gr);
                return (float) juce::Decibels::gainToDecibels (std::abs (L + gg * (1.0 - L)), -60.0);
            }
            case Monitor::Plosive:
            {
                const auto c = val (dp_mode) > 0.5f
                    ? vr::makeSvf (vr::Bell, fs, val (dp_freq), -0.5 * gr, vr::DePlosive::qForWidth (val (dp_width)))
                    : vr::makeSvf (vr::LowShelf, fs, val (dp_freq) * 1.5, -0.5 * gr, 0.70710678);
                return 2.0f * (float) juce::Decibels::gainToDecibels (vr::svfMagnitude (c, fs, f), -60.0);
            }
            case Monitor::Glue:
            {
                if (val (gl_on) < 0.5f) return 0.0f;
                double m = 1.0;
                for (auto& c : vr::Glue::toneFilters ((int) val (gl_mode), val (gl_color) * 0.01f, fs * vr::Glue::kOS))
                    m *= vr::svfMagnitude (c, fs * vr::Glue::kOS, f);
                const float mix = val (gl_mix) * 0.01f;
                return (float) juce::Decibels::gainToDecibels (mix * m + (1.0 - mix), -60.0) - gr + val (gl_out);
            }
            case Monitor::Noise: case Monitor::Background: case Monitor::Isolator: case Monitor::Breath: case Monitor::Verb:
            {
                const int mask = module == Monitor::Noise ? vr::SpectralRepair::MNoise
                               : module == Monitor::Background ? vr::SpectralRepair::MBackground
                               : module == Monitor::Isolator ? vr::SpectralRepair::MIsolator
                               : module == Monitor::Breath ? vr::SpectralRepair::MBreath : vr::SpectralRepair::MVerb;
                const float pos = (float) (std::log (f / 20.0) / std::log (1000.0)) * vr::SpectralRepair::kPts;
                const int i = juce::jlimit (0, vr::SpectralRepair::kPts - 1, (int) pos);
                return proc.spectral.maskDb[(size_t) mask][(size_t) i].load() * on;
            }
            case Monitor::DeEss:
            {
                if (val (de_mode) > 0.5f) return -gr;
                const auto c = vr::makeSvf (vr::HighShelf, fs, val (de_freq) * 0.85, -0.5 * gr, 0.70710678);
                return 2.0f * (float) juce::Decibels::gainToDecibels (vr::svfMagnitude (c, fs, f), -60.0);
            }
            case Monitor::Fet: case Monitor::Opto:
                return -gr;
            case Monitor::Eq:
            {
                if (val (eq_on) < 0.5f) return 0.0f;
                double m = 1.0;
                for (int b = 0; b < 8; ++b)
                {
                    if (val (eqId (b, "on")) < 0.5f) continue;
                    const auto c = vr::makeSvf ((int) val (eqId (b, "type")), fs, val (eqId (b, "freq")),
                                                proc.eq.liveGain[(size_t) b].load(), val (eqId (b, "q")));
                    m *= vr::svfMagnitude (c, fs, f);
                }
                return (float) juce::Decibels::gainToDecibels (m, -60.0);
            }
            default: return 0.0f;
        }
    }

    VoxRehabProcessor& proc;
    int module = Monitor::DeEss;
    float spanSeconds = 5.0f, grHold = 0, grPeakView = 0;
    std::vector<Col> cols;
    Analyser inA, outA;
    std::array<float, Analyser::kPoints> overallSmooth {};
};
} // namespace ui
