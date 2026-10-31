#include "PluginEditor.h"

using namespace ui;

static juce::String u8 (const char* t) { return juce::String (juce::CharPointer_UTF8 (t)); }

namespace accent
{
    const juce::Colour input   { 0xff8fa3b8 }, hum   { 0xffe0b84a }, click { 0xfff08a5d }, plos { 0xffff6b6b },
                       noise   { 0xff4fc3f7 }, bg    { 0xff26c6da }, iso   { 0xffab8cff }, breath { 0xff9ccc65 },
                       verb    { 0xff7986cb }, ess   { 0xffffd54f }, fet   { 0xffff8a65 }, opto { 0xffffb74d },
                       art     { 0xffb0bec5 }, out   { 0xffe6e6e6 }, eq { 0xff64ffda }, glue { 0xffd4a373 };
}

namespace
{
constexpr int kMargin = 12, kGap = 8, kRowsTop = 394;
}

ModulePanel& VoxRehabEditor::panel (const juce::String& title, juce::Colour c, const juce::String& power)
{
    auto* p = panels.add (new ModulePanel (proc.apvts, title, c, power));
    content.addAndMakeVisible (p);
    return *p;
}

VoxRehabEditor::VoxRehabEditor (VoxRehabProcessor& p)
    : AudioProcessorEditor (&p), proc (p), eqDisplay (p), monitorView (p), bandEditor (p.apvts)
{
    setLookAndFeel (&look);
    addAndMakeVisible (content);

    // header
    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    prevBtn.onClick = [this] { presetBox.setSelectedId (juce::jmax (1, presetBox.getSelectedId() - 1)); };
    nextBtn.onClick = [this] { presetBox.setSelectedId (juce::jmin (presetBox.getNumItems(), presetBox.getSelectedId() + 1)); };
    resetOrderBtn.setTooltip ("Put the modules back in the default processing order");
    resetOrderBtn.onClick = [this] { proc.setOrder (VoxRehabProcessor::defaultOrder()); layoutChain (true); };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &prevBtn, &nextBtn, &resetOrderBtn, &eqDisplay, &bandEditor })
        content.addAndMakeVisible (c);

    eqDisplay.onSelect = [this] (int b) { bandEditor.setBand (b); };

    // main display: EQ or Process Monitor for one module
    content.addChildComponent (monitorView);
    viewBox.addItem ("8-Band EQ", 1);
    viewBox.addSeparator();
    viewBox.addSectionHeading ("Process Monitor");
    for (int m = 0; m < Monitor::kNum; ++m) viewBox.addItem (ui::moduleInfo (m).name, 100 + m);
    viewBox.setSelectedId (1, juce::dontSendNotification);
    viewBox.setTooltip ("Choose what the big display shows. Tip: click any module's title to monitor it.");
    viewBox.onChange = [this] { const int id = viewBox.getSelectedId(); showView (id >= 100 ? id - 100 : -1); };
    spanBox.addItemList ({ "2 s", "5 s", "10 s" }, 1);
    spanBox.setSelectedId (2, juce::dontSendNotification);
    spanBox.onChange = [this] { const float sp[] { 2.0f, 5.0f, 10.0f }; monitorView.setSpanSeconds (sp[juce::jlimit (0, 2, spanBox.getSelectedId() - 1)]); };
    content.addAndMakeVisible (viewBox);
    content.addChildComponent (spanBox);
    bandEditor.onSelect = [this] (int b) { eqDisplay.selected = b; bandEditor.setBand (b); eqDisplay.repaint(); };

    // ---------------- modules
    pInput = &panel ("INPUT + LOW CUT", accent::input, ids::hpf_on);
    pInput->addKnob (ids::in_gain, "Trim"); pInput->addKnob (ids::hpf_freq, "Low Cut");
    pInput->addCombo (ids::hpf_slope);

    pHum = &panel ("DE-HUM", accent::hum, ids::hum_on);
    pHum->addKnob (ids::hum_harm, "Harmonics"); pHum->addKnob (ids::hum_q, "Narrow"); pHum->addKnob (ids::hum_depth, "Depth");
    pHum->addCombo (ids::hum_freq);
    pHum->meter = [this] { return proc.deHum.reduction.load(); }; pHum->meterRangeDb = 60;

    pClick = &panel ("DE-CLICK", accent::click, ids::dc_on);
    pClick->addKnob (ids::dc_sens, "Sensitivity"); pClick->addKnob (ids::dc_width, "Width");
    pClick->addKnob (ids::dc_focus, "Focus"); pClick->addKnob (ids::dc_depth, "Depth");
    pClick->meter = [this] { return clickFlash > 0 ? proc.deClick.activity.load() + 12.0f : proc.deClick.activity.load(); };
    pClick->statusText = [this] { return juce::String (proc.deClick.clicks.load()) + " fixed"; };

    pPlos = &panel ("DE-PLOSIVE", accent::plos, ids::dp_on);
    pPlos->addKnob (ids::dp_sens, "Sensitivity"); pPlos->addKnob (ids::dp_freq, "Freq");
    pPlos->addKnob (ids::dp_width, "Width"); pPlos->addKnob (ids::dp_depth, "Depth"); pPlos->addKnob (ids::dp_release, "Release");
    pPlos->addCombo (ids::dp_mode).setTooltip ("Low Band: duck everything below Freq.  Band: duck only the band around Freq, Width octaves wide.");
    pPlos->meter = [this] { return proc.dePlosive.reduction.load(); };

    pNoise = &panel ("DE-NOISE", accent::noise, ids::dn_on);
    pNoise->addKnob (ids::dn_amount, "Sensitivity"); pNoise->addKnob (ids::dn_reduction, "Reduction");
    pNoise->addKnob (ids::dn_tilt, "Tilt");
    pNoise->addCombo (ids::dn_mode);
    pNoise->addToggle (ids::dn_learn, "LEARN", "Hold on while only noise plays (2+ s), then turn off. Switch mode to Learned to use it.");
    pNoise->meter = [this] { return proc.spectral.mDn.load(); };
    pNoise->statusText = [this] { return proc.hasLearnedProfile() ? juce::String ("profile ok") : juce::String ("no profile"); };

    pBg = &panel ("BACKGROUND", accent::bg, ids::bg_on);
    pBg->addKnob (ids::bg_thresh, "Gate Thresh"); pBg->addKnob (ids::bg_depth, "Gate Depth");
    pBg->addKnob (ids::bg_release, "Release"); pBg->addKnob (ids::bg_range, "Range"); pBg->addKnob (ids::bg_amount, "Under Voice");
    pBg->meter = [this] { return proc.spectral.mBg.load(); };

    pIso = &panel ("VOICE ISOLATOR", accent::iso, ids::vi_on);
    pIso->addKnob (ids::vi_amount, "Amount"); pIso->addKnob (ids::vi_low, "Low"); pIso->addKnob (ids::vi_high, "High");
    pIso->addKnob (ids::vi_focus, "Focus");
    pIso->meter = [this] { return proc.spectral.mVi.load(); };
    pIso->statusText = [this]
    {
        const float c = proc.spectral.mConf.load();
        return c > 0.2f ? juce::String (juce::roundToInt (proc.spectral.mPitch.load())) + " Hz" : juce::String ("--");
    };

    pBreath = &panel ("BREATH", accent::breath, ids::br_on);
    pBreath->addKnob (ids::br_thresh, "Below Peak"); pBreath->addKnob (ids::br_reduce, "Reduce");
    pBreath->meter = [this] { return proc.spectral.mBr.load(); };

    pVerb = &panel ("DE-VERB", accent::verb, ids::dv_on);
    pVerb->addKnob (ids::dv_amount, "Amount"); pVerb->addKnob (ids::dv_decay, "Room Decay");
    pVerb->addKnob (ids::dv_early, "Early Keep"); pVerb->addKnob (ids::dv_floor, "Floor");
    pVerb->meter = [this] { return proc.spectral.mDv.load(); };

    pArt = &panel ("ARTIFACTS", accent::art);
    pArt->addKnob (ids::sp_smooth, "Smoothing");

    pEss = &panel ("DE-ESSER", accent::ess, ids::de_on);
    pEss->addKnob (ids::de_freq, "Freq"); pEss->addKnob (ids::de_thresh, "Thresh");
    pEss->addKnob (ids::de_range, "Range"); pEss->addKnob (ids::de_release, "Release");
    pEss->addCombo (ids::de_mode);
    pEss->addToggle (ids::de_listen, "LISTEN", "Hear only the band being de-essed");
    pEss->meter = [this] { return proc.deEsser.reduction.load(); };

    pFet = &panel ("FET COMPRESSOR", accent::fet, ids::c_on);
    pFet->addKnob (ids::c_thresh, "Thresh"); pFet->addKnob (ids::c_ratio, "Ratio"); pFet->addKnob (ids::c_attack, "Attack");
    pFet->addKnob (ids::c_release, "Release"); pFet->addKnob (ids::c_knee, "Knee"); pFet->addKnob (ids::c_makeup, "Makeup");
    pFet->addKnob (ids::c_mix, "Mix"); pFet->addKnob (ids::c_schpf, "SC HPF");
    pFet->addToggle (ids::c_auto, "AUTO GAIN");
    pFet->addToggle (ids::c_allbtn, "ALL BUTTONS", "1176-style all-buttons-in: 20:1 with extra bite");
    pFet->meter = [this] { return proc.fet.reduction.load(); };
    pFet->statusText = [this] { return juce::String (-proc.fet.reduction.load(), 1) + " dB"; };

    pOpto = &panel ("OPTO COMPRESSOR", accent::opto, ids::op_on);
    pOpto->addKnob (ids::op_peak, "Peak Red."); pOpto->addKnob (ids::op_gain, "Gain");
    pOpto->addKnob (ids::op_emph, "HF Emph"); pOpto->addKnob (ids::op_mix, "Mix");
    pOpto->addCombo (ids::op_mode);
    pOpto->meter = [this] { return proc.opto.reduction.load(); };
    pOpto->statusText = [this] { return juce::String (-proc.opto.reduction.load(), 1) + " dB"; };

    pEq = &panel ("8-BAND EQ", accent::eq, ids::eq_on);
    pEq->reservedRight = -1;
    pEq->paintExtra = [this] (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto pl = r.reduced (2, 6).toFloat();
        g.setColour (col::bg);
        g.fillRoundedRectangle (pl, 4.0f);
        const double fs = proc.getCurrentSampleRate();
        std::vector<vr::SvfCoefs> cs;
        for (int b = 0; b < 8; ++b)
            if (proc.apvts.getRawParameterValue (eqId (b, "on"))->load() > 0.5f)
                cs.push_back (vr::makeSvf ((int) proc.apvts.getRawParameterValue (eqId (b, "type"))->load(), fs,
                                           proc.apvts.getRawParameterValue (eqId (b, "freq"))->load(),
                                           proc.eq.liveGain[(size_t) b].load(),
                                           proc.apvts.getRawParameterValue (eqId (b, "q"))->load()));
        g.setColour (col::border);
        g.drawHorizontalLine ((int) pl.getCentreY(), pl.getX(), pl.getRight());
        juce::Path path;
        const int W = (int) pl.getWidth();
        for (int x = 0; x <= W; x += 2)
        {
            const double f = 20.0 * std::pow (1000.0, x / (double) W);
            double m = 1.0;
            for (auto& c : cs) m *= vr::svfMagnitude (c, fs, f);
            const float db = (float) juce::Decibels::gainToDecibels (m, -30.0);
            const float y = juce::jlimit (pl.getY(), pl.getBottom(), pl.getCentreY() - db / 18.0f * pl.getHeight() * 0.5f);
            if (x == 0) path.startNewSubPath (pl.getX() + x, y); else path.lineTo (pl.getX() + x, y);
        }
        g.setColour (accent::eq);
        g.strokePath (path, juce::PathStrokeType (1.6f));
        int on = 0, dyn = 0;
        for (int b = 0; b < 8; ++b)
            if (proc.apvts.getRawParameterValue (eqId (b, "on"))->load() > 0.5f)
            {
                ++on;
                if (proc.apvts.getRawParameterValue (eqId (b, "dyn"))->load() > 0.5f) ++dyn;
            }
        g.setColour (col::dim);
        g.setFont (font (9.5f));
        g.drawText (juce::String (on) + " bands" + (dyn > 0 ? ", " + juce::String (dyn) + " dynamic" : juce::String()),
                    pl.reduced (5, 3), juce::Justification::topLeft);
        g.drawText ("click title to edit", pl.reduced (4, 2), juce::Justification::bottomRight);
    };

    pGlue = &panel ("GLUE / CONSOLE", accent::glue, ids::gl_on);
    pGlue->addKnob (ids::gl_drive, "Drive"); pGlue->addKnob (ids::gl_color, "Colour"); pGlue->addKnob (ids::gl_glue, "Glue");
    pGlue->addKnob (ids::gl_mix, "Mix"); pGlue->addKnob (ids::gl_out, "Output");
    pGlue->addCombo (ids::gl_mode).setTooltip ("Vintage Tape, Siemens-style tube console, API-style or Neve-style console");
    pGlue->meter = [this] { return proc.glue.reduction.load(); };
    pGlue->statusText = [this] { return juce::String (-proc.glue.reduction.load(), 1) + " dB"; };

    pOut = &panel ("OUTPUT", accent::out);
    pOut->addKnob (ids::out_gain, "Output"); pOut->addKnob (ids::mix, "Mix");
    pOut->addToggle (ids::delta, u8 ("\xce\x94 LISTEN REMOVED"), "Hear only what the plugin is taking away (artifact check)");
    pOut->reservedRight = 70;
    pOut->paintExtra = [this] (juce::Graphics& g, juce::Rectangle<int> r)
    {
        auto bar = [&] (juce::Rectangle<int> a, float db, const juce::String& lab)
        {
            g.setColour (col::dim); g.setFont (font (10.0f, true));
            g.drawText (lab, a.removeFromBottom (14), juce::Justification::centred);
            auto m = a.withSizeKeepingCentre (12, a.getHeight()).toFloat();
            g.setColour (col::bg); g.fillRoundedRectangle (m, 3.0f);
            const float v = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            auto f = m.withTrimmedTop (m.getHeight() * (1.0f - v));
            g.setColour (db > -1.0f ? juce::Colour (0xffff5252) : (db > -9.0f ? juce::Colour (0xffffd54f) : juce::Colour (0xff64ffda)));
            g.fillRoundedRectangle (f, 3.0f);
        };
        r = r.reduced (4, 2);
        bar (r.removeFromLeft (r.getWidth() / 2), inMeter, "IN");
        bar (r, outMeter, "OUT");
    };

    // ---------------- chain structure
    using VP = VoxRehabProcessor;
    stagePanels[VP::StHum]      = { pHum };
    stagePanels[VP::StClick]    = { pClick };
    stagePanels[VP::StPlosive]  = { pPlos };
    stagePanels[VP::StSpectral] = { pNoise, pBg, pIso, pBreath, pVerb, pArt };
    stagePanels[VP::StDeEss]    = { pEss };
    stagePanels[VP::StFet]      = { pFet };
    stagePanels[VP::StOpto]     = { pOpto };
    stagePanels[VP::StEq]       = { pEq };
    stagePanels[VP::StGlue]     = { pGlue };
    preferredWidth = { { pInput, 190 }, { pHum, 270 }, { pClick, 280 }, { pPlos, 340 }, { pNoise, 300 }, { pBg, 310 },
                       { pIso, 270 }, { pBreath, 150 }, { pVerb, 270 }, { pArt, 110 }, { pEss, 290 }, { pFet, 490 },
                       { pOpto, 280 }, { pEq, 170 }, { pGlue, 340 }, { pOut, 250 } };

    for (int st = 0; st < VP::kStages; ++st)
        for (auto* pp : stagePanels[(size_t) st])
        {
            pp->draggable = true;
            pp->onDragMove = [this, st] (juce::Point<float> pt) { dragMove (st, pt); };
            pp->onDragEnd = [this, st] (juce::Point<float> pt) { dragEnd (st, pt); };
        }

    panelModule = { { pInput, Monitor::Overall }, { pHum, Monitor::Hum }, { pClick, Monitor::Click }, { pPlos, Monitor::Plosive },
                    { pNoise, Monitor::Noise }, { pBg, Monitor::Background }, { pIso, Monitor::Isolator }, { pBreath, Monitor::Breath },
                    { pVerb, Monitor::Verb }, { pEss, Monitor::DeEss }, { pFet, Monitor::Fet }, { pOpto, Monitor::Opto },
                    { pArt, Monitor::Noise }, { pOut, Monitor::Overall }, { pGlue, Monitor::Glue }, { pEq, Monitor::Eq } };
    for (auto& [pp, m] : panelModule)
    {
        const int mod = m;
        if (pp == pEq) pp->onTitleClick = [this] { viewBox.setSelectedId (1, juce::sendNotificationSync); };
        else           pp->onTitleClick = [this, mod] { viewBox.setSelectedId (100 + mod, juce::sendNotificationSync); };
        pp->setTooltip (pp->draggable ? "Click the title to monitor this module - drag the title to move it in the chain"
                                      : "Click the title to monitor this module");
    }

    for (auto* pp : panels) pp->updateEnabled();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (kW / 2, kH / 2, (int) (kW * 1.3), (int) (kH * 1.3));
    setSize ((int) (kW * 0.75), (int) (kH * 0.75));
    startTimerHz (30);
}

VoxRehabEditor::~VoxRehabEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VoxRehabEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void VoxRehabEditor::showView (int m)
{
    const bool mon = m >= 0;
    eqDisplay.setVisible (! mon);
    monitorView.setVisible (mon);
    spanBox.setVisible (mon);
    if (mon) monitorView.setModule (m);
    for (auto& [pp, mod] : panelModule)
        pp->monitored = (mon && mod == m && pp != pArt && pp != pInput) || (! mon && pp == pEq);
    for (auto* pp : panels) pp->repaint();
}

void VoxRehabEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::text);
    g.setFont (font (24.0f, true));
    g.drawText ("VOX REHAB", 16, 8, 200, 32, juce::Justification::centredLeft);
    g.setColour (col::dim);
    g.setFont (font (12.0f));
    g.drawText ("vocal repair + mix suite", 172, 12, 220, 28, juce::Justification::centredLeft);
    g.drawText (chainText, 860, 10, 688, 20, juce::Justification::centredRight);
    g.drawText (latencyText, 860, 30, 688, 16, juce::Justification::centredRight);

    // the five spectral modules share one analysis pass: show them as one block
    for (auto& r : groupRects)
    {
        g.setColour (accent::noise.withAlpha (0.07f));
        g.fillRoundedRectangle (r.toFloat(), 9.0f);
        g.setColour (accent::noise.withAlpha (0.35f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 9.0f, 1.0f);
    }
}

void VoxRehabEditor::Content::paintOverChildren (juce::Graphics& g)
{
    if (! dropBar.isEmpty())
    {
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (dropBar.toFloat(), 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRoundedRectangle (dropBar.toFloat().expanded (3.0f, 0.0f), 3.0f);
    }
}

void VoxRehabEditor::resized()
{
    content.setBounds (0, 0, kW, kH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));

    presetBox.setBounds (420, 12, 300, 26);
    prevBtn.setBounds (392, 12, 24, 26);
    nextBtn.setBounds (724, 12, 24, 26);
    resetOrderBtn.setBounds (756, 12, 96, 26);

    eqDisplay.setBounds (kMargin, 56, 1060, 330);
    monitorView.setBounds (eqDisplay.getBounds());
    viewBox.setBounds (kMargin + 10, 62, 210, 22);
    spanBox.setBounds (kMargin + 1060 - 10 - 80, 62, 80, 22);
    bandEditor.setBounds (kMargin + 1060 + kGap, 56, kW - 2 * kMargin - 1060 - kGap, 330);

    layoutChain (false);
}

// Panels flow left->right, top->bottom in processing order; each row is stretched to full width.
void VoxRehabEditor::layoutChain (bool animate)
{
    shownOrder = proc.getOrder();
    std::vector<ModulePanel*> seq { pInput };
    for (int st : shownOrder) for (auto* pp : stagePanels[(size_t) st]) seq.push_back (pp);
    seq.push_back (pOut);

    const int rowW = kW - 2 * kMargin;
    std::vector<std::vector<ModulePanel*>> rows (1);
    int x = 0;
    for (auto* pp : seq)
    {
        const int w = preferredWidth[pp];
        if (x > 0 && x + w > rowW * 1.04) { rows.emplace_back(); x = 0; }   // rows may compress a little before wrapping
        rows.back().push_back (pp);
        x += w + kGap;
    }
    const int nRows = (int) rows.size();
    const int avail = kH - kMargin - kRowsTop;
    const int rowH = juce::jmin (205, (avail - (nRows - 1) * kGap) / nRows);

    std::map<ModulePanel*, juce::Rectangle<int>> target;
    for (int r = 0; r < nRows; ++r)
    {
        auto& row = rows[(size_t) r];
        int sum = 0;
        for (auto* pp : row) sum += preferredWidth[pp];
        const int free = rowW - sum - kGap * ((int) row.size() - 1);
        const bool last = r == nRows - 1;
        const double scale = (last && free > rowW / 3) ? 1.0 : 1.0 + (double) free / (double) juce::jmax (1, sum);
        int xx = kMargin;
        const int y = kRowsTop + r * (rowH + kGap);
        for (size_t i = 0; i < row.size(); ++i)
        {
            int w = (int) std::round (preferredWidth[row[i]] * scale);
            if (i == row.size() - 1 && scale != 1.0) w = kMargin + rowW - xx;   // absorb rounding
            target[row[i]] = { xx, y, w, rowH };
            xx += w + kGap;
        }
    }

    // step numbers
    pInput->stepNumber = 1;
    int step = 2;
    for (int st : shownOrder) { for (auto* pp : stagePanels[(size_t) st]) pp->stepNumber = step; ++step; }
    pOut->stepNumber = step;

    for (auto& [pp, b] : target)
    {
        if (animate) juce::Desktop::getInstance().getAnimator().animateComponent (pp, b, 1.0f, 220, false, 1.0, 1.0);
        else pp->setBounds (b);
    }

    // spectral group outline (per row)
    content.groupRects.clear();
    std::map<int, juce::Rectangle<int>> byRow;
    for (auto* pp : stagePanels[VoxRehabProcessor::StSpectral])
    {
        auto b = target[pp];
        byRow[b.getY()] = byRow.count (b.getY()) ? byRow[b.getY()].getUnion (b) : b;
    }
    for (auto& [y, r] : byRow) content.groupRects.push_back (r.expanded (4, 4));

    // header chain text
    juce::String t = u8 ("chain: low cut \xe2\x86\x92 ");
    for (int st : shownOrder) t << VoxRehabProcessor::stageName (st) << u8 (" \xe2\x86\x92 ");
    t << "out";
    content.chainText = t;
    content.repaint();
}

int VoxRehabEditor::insertionIndexFor (juce::Point<float> p, juce::Rectangle<int>& bar) const
{
    // find the panel under (or nearest to) the pointer
    const ModulePanel* best = nullptr; float bestD = 1.0e9f;
    for (auto* pp : panels)
    {
        auto b = pp->getBounds().toFloat().expanded (kGap * 0.5f);
        const float d = b.contains (p) ? 0.0f : b.getCentre().getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = pp; }
    }
    if (best == nullptr) return -1;
    auto barAt = [&] (const ModulePanel* pp, bool left)
    {
        auto b = pp->getBounds();
        bar = juce::Rectangle<int> (left ? b.getX() - kGap / 2 - 2 : b.getRight() + kGap / 2 - 2, b.getY(), 4, b.getHeight());
    };
    if (best == pInput) { barAt (pInput, false); return 0; }
    if (best == pOut)   { barAt (pOut, true); return VoxRehabProcessor::kStages; }
    for (int i = 0; i < VoxRehabProcessor::kStages; ++i)
    {
        const auto& ps = stagePanels[(size_t) shownOrder[(size_t) i]];
        for (size_t k = 0; k < ps.size(); ++k)
            if (ps[k] == best)
            {
                const bool before = k == 0 && p.x < (float) best->getBounds().getCentreX();
                if (before) { barAt (ps.front(), true); return i; }
                barAt (ps.back(), false);
                return i + 1;
            }
    }
    return -1;
}

void VoxRehabEditor::dragMove (int, juce::Point<float> p)
{
    juce::Rectangle<int> bar;
    if (insertionIndexFor (p, bar) < 0) bar = {};
    if (bar != content.dropBar) { content.dropBar = bar; content.repaint(); }
}

void VoxRehabEditor::dragEnd (int stage, juce::Point<float> p)
{
    juce::Rectangle<int> bar;
    int idx = insertionIndexFor (p, bar);
    content.dropBar = {};
    content.repaint();
    if (idx < 0) return;
    Order o = proc.getOrder();
    int from = -1;
    for (int i = 0; i < VoxRehabProcessor::kStages; ++i) if (o[(size_t) i] == stage) from = i;
    if (from < 0) return;
    if (idx > from) --idx;
    std::vector<int> v (o.begin(), o.end());
    v.erase (v.begin() + from);
    v.insert (v.begin() + juce::jlimit (0, (int) v.size(), idx), stage);
    std::copy (v.begin(), v.end(), o.begin());
    proc.setOrder (o);
    layoutChain (true);
}

void VoxRehabEditor::timerCallback()
{
    if (eqDisplay.isVisible()) eqDisplay.tick();
    if (monitorView.isVisible()) monitorView.tick();
    bandEditor.refreshDynEnabled();
    if (proc.getOrder() != shownOrder) layoutChain (true);    // e.g. session/state reloaded
    const float in = proc.inPeak.load(), out = proc.outPeak.load();
    inMeter = in > inMeter ? in : inMeter - 1.5f;
    outMeter = out > outMeter ? out : outMeter - 1.5f;
    const int c = proc.deClick.clicks.load();
    if (c != lastClicks) { clickFlash = 4; lastClicks = c; } else if (clickFlash > 0) --clickFlash;
    const double fs = proc.getCurrentSampleRate();
    content.latencyText = "latency " + juce::String (1000.0 * proc.getLatencySamples() / juce::jmax (1.0, fs), 1)
                        + " ms (auto-compensated by your DAW)  |  drag module titles to reorder";
    for (auto* p : panels) p->repaint();
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    content.repaint (860, 28, 688, 20);
}
