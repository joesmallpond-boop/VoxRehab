#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
struct Preset { const char* name; std::vector<std::pair<juce::String, float>> values; };

std::vector<Preset> makePresets()
{
    using namespace ids;
    auto eq = [] (int b, const char* f) { return eqId (b, f); };
    return {
        { "Gentle Repair (default)", {} },
        { "Home Studio Rescue", { { dn_reduction, 14 }, { bg_on, 1 }, { dv_on, 1 }, { dv_amount, 35 }, { dv_decay, 0.5f },
                                  { de_thresh, -30 }, { c_thresh, -20 }, { op_on, 1 }, { op_peak, 35 },
                                  { eq (2, "on"), 1 }, { eq (2, "gain"), -2.5f }, { eq (6, "on"), 1 }, { eq (6, "gain"), 2 } } },
        { "Phone / Laptop Mic", { { hpf_freq, 110 }, { dn_amount, 55 }, { dn_reduction, 18 }, { bg_on, 1 }, { vi_on, 1 },
                                  { dv_on, 1 }, { dv_amount, 50 }, { eq (2, "on"), 1 }, { eq (2, "freq"), 350 }, { eq (2, "gain"), -3 },
                                  { eq (4, "on"), 1 }, { eq (4, "freq"), 3000 }, { eq (4, "gain"), 2.5f },
                                  { eq (6, "on"), 1 }, { eq (6, "gain"), 3 } } },
        { "Noisy Room", { { dn_amount, 60 }, { dn_reduction, 22 }, { bg_on, 1 }, { bg_thresh, -50 }, { bg_depth, 18 },
                          { vi_on, 1 }, { vi_amount, 45 }, { br_on, 1 } } },
        { "Boomy / Echoey Room", { { dv_on, 1 }, { dv_amount, 70 }, { dv_decay, 1.2f }, { dv_early, 30 },
                                   { eq (1, "on"), 1 }, { eq (1, "freq"), 150 }, { eq (1, "gain"), -3 },
                                   { eq (2, "on"), 1 }, { eq (2, "freq"), 260 }, { eq (2, "gain"), -3 } } },
        { "Poppy & Sibilant", { { dp_sens, 70 }, { dp_depth, 24 }, { de_thresh, -34 }, { de_range, 10 },
                                { eq (5, "on"), 1 }, { eq (5, "freq"), 6500 }, { eq (5, "dyn"), 1 }, { eq (5, "thresh"), -35 },
                                { eq (5, "range"), -6 } } },
        { "Podcast Polish", { { hpf_freq, 80 }, { c_thresh, -22 }, { c_ratio, 4 }, { op_on, 1 }, { op_peak, 45 },
                              { br_on, 1 }, { dn_reduction, 12 }, { eq (2, "on"), 1 }, { eq (2, "gain"), -2 },
                              { eq (6, "on"), 1 }, { eq (6, "gain"), 2.5f } } },
        { "Rap Vocal Punch", { { c_attack, 0.1f }, { c_release, 60 }, { c_ratio, 8 }, { c_thresh, -24 }, { op_on, 1 },
                               { op_peak, 30 }, { dp_sens, 65 }, { de_thresh, -30 },
                               { eq (4, "on"), 1 }, { eq (4, "freq"), 3000 }, { eq (4, "gain"), 2 },
                               { eq (6, "on"), 1 }, { eq (6, "gain"), 3 } } },
        { "Hum Kill (50 Hz)", { { hum_on, 1 }, { hum_freq, 0 } } },
        { "Hum Kill (60 Hz)", { { hum_on, 1 }, { hum_freq, 1 } } },
        { "Transparent (all off)", { { hpf_on, 0 }, { dc_on, 0 }, { dp_on, 0 }, { dn_on, 0 }, { de_on, 0 }, { c_on, 0 } } },
    };
}

const std::vector<Preset>& presets() { static const auto p = makePresets(); return p; }
} // namespace

VoxRehabProcessor::VoxRehabProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VoxRehab", createParameterLayout())
{
}

bool VoxRehabProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void VoxRehabProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentFs = sampleRate;
    maxChunk = juce::jlimit (32, 1024, samplesPerBlock);
    const int nch = 2;
    tickSamples = juce::jmax (32, (int) std::round (sampleRate * (128.0 / 48000.0)));
    tickPos = 0; accCount = 0;
    accPre.fill (0); accPost.fill (0); accGr.fill (0); accSide.fill (-120.0f); accSide2.fill (-120.0f); accEv.fill (false);
    monitor.tickSeconds.store ((float) (tickSamples / sampleRate));
    input.prepare (sampleRate, nch);
    deHum.prepare (sampleRate, nch);
    deClick.prepare (sampleRate, nch);
    dePlosive.prepare (sampleRate, nch);
    spectral.prepare (sampleRate, nch);
    deEsser.prepare (sampleRate, nch);
    fet.prepare (sampleRate, nch);
    opto.prepare (sampleRate, nch);
    eq.prepare (sampleRate, nch);

    latency = deClick.getLatency() + dePlosive.getLatency() + spectral.getLatency();
    setLatencySamples (latency);
    {
        const float t = (float) tickSamples;
        auto lat = [&] (int m, int samples) { monitor.tr[(size_t) m].latencyTicks.store ((float) samples / t); };
        for (int m = 0; m < Monitor::kNum; ++m) lat (m, 0);
        lat (Monitor::Overall, latency);
        lat (Monitor::Click, deClick.getLatency());
        lat (Monitor::Plosive, dePlosive.getLatency());
        for (int m : { (int) Monitor::Noise, (int) Monitor::Background, (int) Monitor::Isolator, (int) Monitor::Breath, (int) Monitor::Verb })
            lat (m, spectral.getLatency());
    }

    const int ringSize = vr::nextPow2 (latency + maxChunk + 8);
    dryMask = ringSize - 1;
    dryRing.assign ((size_t) nch, std::vector<float> ((size_t) ringSize, 0.0f));
    dryBuf.assign ((size_t) nch, std::vector<float> ((size_t) maxChunk, 0.0f));
    dryPos = 0;
    monoTmp.assign ((size_t) maxChunk, 0.0f);
    outGain.reset (sampleRate, 0.03); outGain.setCurrentAndTargetValue (vr::dbToGain (pv (ids::out_gain)));
    mixS.reset (sampleRate, 0.03);    mixS.setCurrentAndTargetValue (pv (ids::mix) * 0.01f);
}

void VoxRehabProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nch = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    if (nch <= 0) return;

    // process in pieces that end on monitor-tick boundaries
    const int total = buffer.getNumSamples();
    for (int start = 0; start < total;)
    {
        const int n = juce::jmin (maxChunk, total - start, tickSamples - tickPos);
        float* ptrs[2] = { buffer.getWritePointer (0, start), nch > 1 ? buffer.getWritePointer (1, start) : nullptr };
        processChunk (ptrs, nch, n);
        start += n;
        tickPos += n;
        if (tickPos >= tickSamples) { commitTick(); tickPos = 0; }
    }
}

void VoxRehabProcessor::commitTick()
{
    const int t = monitor.tick.load (std::memory_order_relaxed);
    const int idx = t & (Monitor::kLen - 1);
    const double cnt = juce::jmax (1.0, accCount);
    for (int m = 0; m < Monitor::kNum; ++m)
    {
        auto& tr = monitor.tr[(size_t) m];
        tr.pre[(size_t) idx]  = (float) (10.0 * std::log10 (accPre[(size_t) m] / cnt + 1.0e-12));
        tr.post[(size_t) idx] = (float) (10.0 * std::log10 (accPost[(size_t) m] / cnt + 1.0e-12));
        tr.gr[(size_t) idx] = accGr[(size_t) m];
        tr.side[(size_t) idx] = accSide[(size_t) m];
        tr.side2[(size_t) idx] = accSide2[(size_t) m];
        tr.ev[(size_t) idx] = accEv[(size_t) m] ? 1 : 0;
    }
    monitor.tick.store (t + 1, std::memory_order_release);
    accPre.fill (0); accPost.fill (0); accGr.fill (0); accSide.fill (-120.0f); accSide2.fill (-120.0f); accEv.fill (false);
    accCount = 0;
}

void VoxRehabProcessor::processChunk (float* const* ch, int nch, int n)
{
    using namespace ids;
    // ---- meters / scope in
    float pk = 0;
    for (int i = 0; i < n; ++i)
    {
        float m = 0;
        for (int c = 0; c < nch; ++c) { m += ch[c][i]; pk = std::max (pk, std::abs (ch[c][i])); }
        monoTmp[(size_t) i] = m / (float) nch;
    }
    scopeIn.push (monoTmp.data(), n);
    inPeak.store (vr::gainToDb (pk));

    // ---- latency-compensated dry copy
    for (int c = 0; c < nch; ++c)
    {
        auto& ring = dryRing[(size_t) c];
        for (int i = 0; i < n; ++i)
        {
            const int w = (dryPos + i) & dryMask;
            ring[(size_t) w] = ch[c][i];
            dryBuf[(size_t) c][(size_t) i] = ring[(size_t) ((w - latency) & dryMask)];
        }
    }
    dryPos = (dryPos + n) & dryMask;

    // ---- chain (each stage's input/output energy is logged for the monitor)
    auto energy = [&] () { double e = 0; for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) e += (double) ch[c][i] * ch[c][i]; return e / nch; };
    auto stage = [&] (int m, double pre, double post) { accPre[(size_t) m] += pre; accPost[(size_t) m] += post; };
    auto gr = [&] (int m, float v) { accGr[(size_t) m] = juce::jmax (accGr[(size_t) m], v); };
    auto side = [&] (int m, float v, float v2 = -120.0f)
    {
        accSide[(size_t) m] = juce::jmax (accSide[(size_t) m], v); accSide2[(size_t) m] = juce::jmax (accSide2[(size_t) m], v2);
    };
    accCount += n;
    const double eIn = energy();
    input.process (ch, nch, n, pv (in_gain), pv (hpf_on) > 0.5f, pv (hpf_freq), pv (hpf_slope) > 0.5f);
    const double e0 = energy();
    deHum.process (ch, nch, n, pv (hum_on) > 0.5f, pv (hum_freq) > 0.5f ? 60.0f : 50.0f,
                   (int) pv (hum_harm), pv (hum_q), pv (hum_depth));
    const double e1 = energy();
    stage (Monitor::Hum, e0, e1);
    if (pv (hum_on) > 0.5f) gr (Monitor::Hum, (float) juce::jmax (0.0, 10.0 * std::log10 ((e0 + 1e-12) / (e1 + 1e-12))));
    deClick.process (ch, nch, n, pv (dc_on) > 0.5f, pv (dc_sens) * 0.01f, pv (dc_width), pv (dc_focus), pv (dc_depth) * 0.01f);
    const double e2 = energy();
    stage (Monitor::Click, e1, e2);
    gr (Monitor::Click, deClick.activity.load());
    { const int c = deClick.clicks.load(); if (c != lastClicks) { accEv[Monitor::Click] = true; lastClicks = c; } }
    dePlosive.process (ch, nch, n, pv (dp_on) > 0.5f, pv (dp_sens) * 0.01f, pv (dp_freq), pv (dp_depth), pv (dp_release));

    const double e3 = energy();
    stage (Monitor::Plosive, e2, e3);
    {
        const float r = dePlosive.reduction.load();
        gr (Monitor::Plosive, r);
        side (Monitor::Plosive, dePlosive.lowDb.load(), dePlosive.highDb.load());
        if (r > 1.0f && lastPlosGr <= 1.0f) accEv[Monitor::Plosive] = true;
        lastPlosGr = r;
    }
    vr::SpectralRepair::Settings s;
    s.dnOn = pv (dn_on) > 0.5f; s.dnAmount = pv (dn_amount) * 0.01f; s.dnReductionDb = pv (dn_reduction);
    s.dnTilt = pv (dn_tilt); s.useLearned = pv (dn_mode) > 0.5f; s.learning = pv (dn_learn) > 0.5f;
    s.bgOn = pv (bg_on) > 0.5f; s.bgThresh = pv (bg_thresh); s.bgDepthDb = pv (bg_depth); s.bgReleaseMs = pv (bg_release);
    s.bgRangeDb = pv (bg_range); s.bgAmount = pv (bg_amount) * 0.01f;
    s.viOn = pv (vi_on) > 0.5f; s.viAmount = pv (vi_amount) * 0.01f; s.viLow = pv (vi_low); s.viHigh = pv (vi_high);
    s.viFocus = pv (vi_focus) * 0.01f;
    s.brOn = pv (br_on) > 0.5f; s.brThreshDb = pv (br_thresh); s.brReduceDb = pv (br_reduce);
    s.dvOn = pv (dv_on) > 0.5f; s.dvAmount = pv (dv_amount) * 0.01f; s.dvDecay = pv (dv_decay); s.dvEarlyMs = pv (dv_early);
    s.dvFloorDb = pv (dv_floor);
    s.smooth = pv (sp_smooth) * 0.01f;
    spectral.process (ch, nch, n, s);

    const double e4 = energy();
    for (int m : { (int) Monitor::Noise, (int) Monitor::Background, (int) Monitor::Isolator, (int) Monitor::Breath, (int) Monitor::Verb })
        stage (m, e3, e4);
    gr (Monitor::Noise, s.dnOn ? spectral.mDn.load() : 0.0f);
    gr (Monitor::Background, s.bgOn ? spectral.mBg.load() : 0.0f);
    gr (Monitor::Isolator, s.viOn ? spectral.mVi.load() : 0.0f);
    side (Monitor::Isolator, spectral.mConf.load() > 0.2f ? spectral.mPitch.load() : -120.0f);
    gr (Monitor::Breath, s.brOn ? spectral.mBr.load() : 0.0f);
    if (s.brOn && spectral.mBr.load() > 1.0f) accEv[Monitor::Breath] = true;
    gr (Monitor::Verb, s.dvOn ? spectral.mDv.load() : 0.0f);
    deEsser.process (ch, nch, n, pv (de_on) > 0.5f, pv (de_freq), pv (de_thresh), pv (de_range), pv (de_release),
                     pv (de_mode) > 0.5f, pv (de_listen) > 0.5f);

    const double e5 = energy();
    stage (Monitor::DeEss, e4, e5);
    gr (Monitor::DeEss, deEsser.reduction.load());
    side (Monitor::DeEss, deEsser.sideDb.load());
    vr::FetCompressor::Settings fs { pv (c_on) > 0.5f, pv (c_thresh), pv (c_ratio), pv (c_knee), pv (c_attack), pv (c_release),
                                     pv (c_makeup), pv (c_auto) > 0.5f, pv (c_allbtn) > 0.5f, pv (c_mix) * 0.01f, pv (c_schpf) };
    fet.process (ch, nch, n, fs);
    const double e6 = energy();
    stage (Monitor::Fet, e5, e6);
    gr (Monitor::Fet, fet.reduction.load());
    side (Monitor::Fet, fet.sideDb.load());
    vr::OptoCompressor::Settings os { pv (op_on) > 0.5f, pv (op_peak) * 0.01f, pv (op_gain), pv (op_mode) > 0.5f,
                                      pv (op_emph) * 0.01f, pv (op_mix) * 0.01f };
    opto.process (ch, nch, n, os);

    const double e7 = energy();
    stage (Monitor::Opto, e6, e7);
    gr (Monitor::Opto, opto.reduction.load());
    side (Monitor::Opto, opto.sideDb.load(), opto.threshDb.load());
    std::array<vr::DynamicEq::Band, 8> bands;
    for (int b = 0; b < 8; ++b)
    {
        auto& bd = bands[(size_t) b];
        bd.on = pv (eqId (b, "on")) > 0.5f; bd.type = (int) pv (eqId (b, "type"));
        bd.freq = pv (eqId (b, "freq")); bd.gain = pv (eqId (b, "gain")); bd.q = pv (eqId (b, "q"));
        bd.dyn = pv (eqId (b, "dyn")) > 0.5f; bd.thresh = pv (eqId (b, "thresh")); bd.ratio = pv (eqId (b, "ratio"));
        bd.range = pv (eqId (b, "range")); bd.attack = pv (eqId (b, "attack")); bd.release = pv (eqId (b, "release"));
    }
    eq.process (ch, nch, n, bands, pv (eq_on) > 0.5f);
    {
        const double e8 = energy();
        stage (Monitor::Eq, e7, e8);
        float dynMax = 0;
        for (int b = 0; b < 8; ++b)
            if (bands[(size_t) b].on && bands[(size_t) b].dyn)
                dynMax = juce::jmax (dynMax, std::abs (eq.liveGain[(size_t) b].load() - bands[(size_t) b].gain));
        gr (Monitor::Eq, dynMax);
    }

    // ---- output: gain, mix, delta
    const bool delta = pv (ids::delta) > 0.5f;
    outGain.setTargetValue (vr::dbToGain (pv (out_gain)));
    mixS.setTargetValue (pv (ids::mix) * 0.01f);
    pk = 0;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue(), m = mixS.getNextValue();
        float mono = 0;
        for (int c = 0; c < nch; ++c)
        {
            const float dry = dryBuf[(size_t) c][(size_t) i];
            float y = delta ? (dry - ch[c][i]) : (m * ch[c][i] * g + (1.0f - m) * dry);
            if (! std::isfinite (y)) y = 0.0f;
            ch[c][i] = y;
            mono += y;
            pk = std::max (pk, std::abs (y));
        }
        monoTmp[(size_t) i] = mono / (float) nch;
    }
    scopeOut.push (monoTmp.data(), n);
    outPeak.store (vr::gainToDb (pk));
    stage (Monitor::Overall, eIn, energy());
}

//==============================================================================
int VoxRehabProcessor::getNumPrograms() { return (int) presets().size(); }
const juce::String VoxRehabProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, (int) presets().size()) ? presets()[(size_t) index].name : "";
}
void VoxRehabProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets().size())) return;
    currentProgram = index;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ids::dn_learn && rp->getParameterID() != ids::dn_mode)
                rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto& [id, v] : presets()[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (v));
}

void VoxRehabProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (spectral.learnedValid)
    {
        juce::MemoryBlock mb (spectral.learned.data(), spectral.learned.size() * sizeof (float));
        state.setProperty ("noiseProfile", mb.toBase64Encoding(), nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void VoxRehabProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        if (! tree.isValid()) return;
        currentProgram = (int) tree.getProperty ("program", 0);
        const auto profile = tree.getProperty ("noiseProfile").toString();
        tree.removeProperty ("noiseProfile", nullptr);
        apvts.replaceState (tree);
        if (profile.isNotEmpty())
        {
            juce::MemoryBlock mb;
            if (mb.fromBase64Encoding (profile))
            {
                const size_t n = mb.getSize() / sizeof (float);
                if (n > 0 && (spectral.learned.empty() || n == spectral.learned.size()))
                {
                    spectral.learned.assign ((const float*) mb.getData(), (const float*) mb.getData() + n);
                    spectral.learnedValid = true;
                }
            }
        }
        if (auto* p = apvts.getParameter (ids::dn_learn)) p->setValueNotifyingHost (0.0f);
    }
}

juce::AudioProcessorEditor* VoxRehabProcessor::createEditor() { return new VoxRehabEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoxRehabProcessor(); }
