#include "PluginEditor.h"

using namespace ui;

static juce::String u8 (const char* t) { return juce::String (juce::CharPointer_UTF8 (t)); }

namespace accent
{
    const juce::Colour input   { 0xff8fa3b8 }, hum   { 0xffe0b84a }, click { 0xfff08a5d }, plos { 0xffff6b6b },
                       noise   { 0xff4fc3f7 }, bg    { 0xff26c6da }, iso   { 0xffab8cff }, breath { 0xff9ccc65 },
                       verb    { 0xff7986cb }, ess   { 0xffffd54f }, fet   { 0xffff8a65 }, opto { 0xffffb74d },
                       art     { 0xffb0bec5 }, out   { 0xffe6e6e6 };
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
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &prevBtn, &nextBtn, &eqDisplay, &bandEditor })
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

    // INPUT
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
    pPlos->addKnob (ids::dp_depth, "Depth"); pPlos->addKnob (ids::dp_release, "Release");
    pPlos->meter = [this] { return proc.dePlosive.reduction.load(); };

    pNoise = &panel ("DE-NOISE", accent::noise, ids::dn_on);
    pNoise->addKnob (ids::dn_amount, "Sensitivity"); pNoise->addKnob (ids::dn_reduction, "Reduction");
    pNoise->addKnob (ids::dn_tilt, "Tilt");
    pNoise->addCombo (ids::dn_mode);
    pNoise->addToggle (ids::dn_learn, "LEARN", "Hold on while only noise plays (2+ s), then turn off. Switch mode to Learned to use it.");
    pNoise->meter = [this] { return proc.spectral.mDn.load(); };
    pNoise->statusText = [this] { return proc.hasLearnedProfile() ? juce::String ("profile ok") : juce::String ("no profile"); };

    pBg = &panel ("BACKGROUND REMOVER", accent::bg, ids::bg_on);
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

    pEss = &panel ("DE-ESSER", accent::ess, ids::de_on);
    pEss->addKnob (ids::de_freq, "Freq"); pEss->addKnob (ids::de_thresh, "Thresh");
    pEss->addKnob (ids::de_range, "Range"); pEss->addKnob (ids::de_release, "Release");
    pEss->addCombo (ids::de_mode);
    pEss->addToggle (ids::de_listen, "LISTEN", "Hear only the band being de-essed");
    pEss->meter = [this] { return proc.deEsser.reduction.load(); };

    pFet = &panel (u8 ("COMP 1 \xc2\xb7 FET (FAST)"), accent::fet, ids::c_on);
    pFet->addKnob (ids::c_thresh, "Thresh"); pFet->addKnob (ids::c_ratio, "Ratio"); pFet->addKnob (ids::c_attack, "Attack");
    pFet->addKnob (ids::c_release, "Release"); pFet->addKnob (ids::c_knee, "Knee"); pFet->addKnob (ids::c_makeup, "Makeup");
    pFet->addKnob (ids::c_mix, "Mix"); pFet->addKnob (ids::c_schpf, "SC HPF");
    pFet->addToggle (ids::c_auto, "AUTO GAIN");
    pFet->addToggle (ids::c_allbtn, "ALL BUTTONS", "1176-style all-buttons-in: 20:1 with extra bite");
    pFet->meter = [this] { return proc.fet.reduction.load(); };
    pFet->statusText = [this] { return "GR " + juce::String (proc.fet.reduction.load(), 1) + " dB"; };

    pOpto = &panel (u8 ("COMP 2 \xc2\xb7 OPTO (POST-FET)"), accent::opto, ids::op_on);
    pOpto->addKnob (ids::op_peak, "Peak Red."); pOpto->addKnob (ids::op_gain, "Gain");
    pOpto->addKnob (ids::op_emph, "HF Emph"); pOpto->addKnob (ids::op_mix, "Mix");
    pOpto->addCombo (ids::op_mode);
    pOpto->meter = [this] { return proc.opto.reduction.load(); };
    pOpto->statusText = [this] { return "GR " + juce::String (proc.opto.reduction.load(), 1) + " dB"; };

    pArt = &panel ("ARTIFACTS", accent::art);
    pArt->addKnob (ids::sp_smooth, "Smoothing");

    pOut = &panel ("OUTPUT", accent::out);
    pOut->addKnob (ids::out_gain, "Output"); pOut->addKnob (ids::mix, "Mix");
    pOut->addToggle (ids::delta, u8 ("\xce\x94 LISTEN REMOVED"), "Hear only what the plugin is taking away (artifact check)");
    pOut->reservedRight = 120;
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

    panelModule = { { pInput, Monitor::Overall }, { pHum, Monitor::Hum }, { pClick, Monitor::Click }, { pPlos, Monitor::Plosive },
                    { pNoise, Monitor::Noise }, { pBg, Monitor::Background }, { pIso, Monitor::Isolator }, { pBreath, Monitor::Breath },
                    { pVerb, Monitor::Verb }, { pEss, Monitor::DeEss }, { pFet, Monitor::Fet }, { pOpto, Monitor::Opto },
                    { pArt, Monitor::Noise }, { pOut, Monitor::Overall } };
    for (auto& [pp, m] : panelModule)
    {
        const int mod = m;
        pp->onTitleClick = [this, mod] { viewBox.setSelectedId (100 + mod, juce::sendNotificationSync); };
        pp->setTooltip ("Click the title to see this module in the Process Monitor");
    }

    for (auto* pp : panels) pp->updateEnabled();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) kW / kH);
    setResizeLimits (kW / 2, kH / 2, (int) (kW * 1.3), (int) (kH * 1.3));
    setSize ((int) (kW * 0.8), (int) (kH * 0.8));
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
    for (auto& [pp, mod] : panelModule) pp->monitored = mon && mod == m && pp != pArt && pp != pInput;
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
    g.drawText (u8 ("chain: low cut \xe2\x86\x92 hum \xe2\x86\x92 click \xe2\x86\x92 plosive \xe2\x86\x92 noise / background / isolate / breath / verb "
                "\xe2\x86\x92 de-ess \xe2\x86\x92 FET \xe2\x86\x92 opto \xe2\x86\x92 EQ \xe2\x86\x92 out"),
                760, 12, 780, 28, juce::Justification::centredRight);
    g.drawText (latencyText, 760, 30, 780, 16, juce::Justification::centredRight);
}

void VoxRehabEditor::resized()
{
    content.setBounds (0, 0, kW, kH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kW));

    presetBox.setBounds (420, 12, 300, 26);
    prevBtn.setBounds (392, 12, 24, 26);
    nextBtn.setBounds (724, 12, 24, 26);

    const int m = 12, gap = 8;
    eqDisplay.setBounds (m, 56, 1060, 330);
    monitorView.setBounds (eqDisplay.getBounds());
    viewBox.setBounds (m + 10, 62, 210, 22);
    spanBox.setBounds (m + 1060 - 10 - 80, 62, 80, 22);
    bandEditor.setBounds (m + 1060 + gap, 56, kW - 2 * m - 1060 - gap, 330);

    auto row = [&] (int y, int h, std::initializer_list<std::pair<ModulePanel*, int>> items)
    {
        int x = m, fixed = 0, flex = 0;
        for (auto& it : items) { if (it.second > 0) fixed += it.second; else ++flex; }
        const int avail = kW - 2 * m - gap * ((int) items.size() - 1) - fixed;
        for (auto& it : items)
        {
            const int w = it.second > 0 ? it.second : avail / juce::jmax (1, flex);
            it.first->setBounds (x, y, w, h);
            x += w + gap;
        }
    };
    row (394, 196, { { pInput, 210 }, { pHum, 300 }, { pClick, 310 }, { pPlos, 310 }, { pNoise, 0 } });
    row (598, 196, { { pBg, 380 }, { pIso, 310 }, { pBreath, 170 }, { pVerb, 310 }, { pEss, 0 } });
    row (802, 196, { { pFet, 620 }, { pOpto, 350 }, { pArt, 120 }, { pOut, 0 } });
}

void VoxRehabEditor::timerCallback()
{
    if (eqDisplay.isVisible()) eqDisplay.tick();
    if (monitorView.isVisible()) monitorView.tick();
    bandEditor.refreshDynEnabled();
    const float in = proc.inPeak.load(), out = proc.outPeak.load();
    inMeter = in > inMeter ? in : inMeter - 1.5f;
    outMeter = out > outMeter ? out : outMeter - 1.5f;
    const int c = proc.deClick.clicks.load();
    if (c != lastClicks) { clickFlash = 4; lastClicks = c; } else if (clickFlash > 0) --clickFlash;
    const double fs = proc.getCurrentSampleRate();
    content.latencyText = "latency " + juce::String (1000.0 * proc.getLatencySamples() / juce::jmax (1.0, fs), 1)
                        + " ms (auto-compensated by your DAW)";
    for (auto* p : panels) p->repaint();
    if (presetBox.getSelectedId() != proc.getCurrentProgram() + 1)
        presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    content.repaint (760, 28, 780, 20);
}
