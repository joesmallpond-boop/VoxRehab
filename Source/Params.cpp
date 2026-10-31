#include "Params.h"

using namespace juce;

namespace
{
String fmtHz (float v)
{
    return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (roundToInt (v)) + " Hz";
}

struct Builder
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    void f (const String& id, const String& name, float lo, float hi, float def, float centre, const String& unit, int decimals = 1)
    {
        NormalisableRange<float> r (lo, hi);
        if (centre > lo && centre < hi) r.setSkewForCentre (centre);
        auto attrs = AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction (
            [unit, decimals] (float v, int)
            {
                if (unit == "Hz") return fmtHz (v);
                if (unit == "%")  return String (roundToInt (v)) + " %";
                if (unit == ":1") return String (v, v < 10 ? 1 : 0) + ":1";
                return String (v, decimals) + (unit.isEmpty() ? String() : " " + unit);
            });
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def, attrs));
    }
    void b (const String& id, const String& name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    }
    void c (const String& id, const String& name, const StringArray& items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    }
    void i (const String& id, const String& name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    }
};
} // namespace

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Builder p;
    // INPUT
    p.f (ids::in_gain, "Input Gain", -24, 24, 0, 0, "dB");
    p.b (ids::hpf_on, "HPF On", true);
    p.f (ids::hpf_freq, "HPF Freq", 20, 400, 70, 90, "Hz");
    p.c (ids::hpf_slope, "HPF Slope", { "12 dB/oct", "24 dB/oct" }, 1);
    // DE-HUM
    p.b (ids::hum_on, "De-Hum On", false);
    p.c (ids::hum_freq, "Hum Freq", { "50 Hz", "60 Hz" }, 0);
    p.i (ids::hum_harm, "Hum Harmonics", 1, 12, 6);
    p.f (ids::hum_q, "Hum Q", 4, 80, 25, 20, "", 0);
    p.f (ids::hum_depth, "Hum Depth", 0, 60, 30, 0, "dB");
    // DE-CLICK
    p.b (ids::dc_on, "De-Click On", true);
    p.f (ids::dc_sens, "Click Sensitivity", 0, 100, 45, 0, "%");
    p.f (ids::dc_width, "Click Width", 0.2f, 3.0f, 1.0f, 1.0f, "ms", 2);
    p.f (ids::dc_focus, "Click Focus", 800, 8000, 2500, 2500, "Hz");
    p.f (ids::dc_depth, "Click Depth", 0, 100, 90, 0, "%");
    // DE-PLOSIVE
    p.b (ids::dp_on, "De-Plosive On", true);
    p.f (ids::dp_sens, "Plosive Sensitivity", 0, 100, 50, 0, "%");
    p.f (ids::dp_freq, "Plosive Freq", 60, 300, 140, 130, "Hz");
    p.f (ids::dp_depth, "Plosive Depth", 0, 36, 18, 0, "dB");
    p.f (ids::dp_release, "Plosive Release", 10, 300, 60, 60, "ms", 0);
    // DE-NOISE
    p.b (ids::dn_on, "De-Noise On", true);
    p.f (ids::dn_amount, "Noise Sensitivity", 0, 100, 40, 0, "%");
    p.f (ids::dn_reduction, "Noise Reduction", 0, 40, 10, 12, "dB");
    p.f (ids::dn_tilt, "Noise Tilt", -3, 3, 0, 0, "dB/oct", 1);
    p.c (ids::dn_mode, "Noise Mode", { "Adaptive", "Learned" }, 0);
    p.b (ids::dn_learn, "Learn Noise", false);
    // BACKGROUND
    p.b (ids::bg_on, "Background On", false);
    p.f (ids::bg_thresh, "Background Threshold", -90, -20, -55, -55, "dB");
    p.f (ids::bg_depth, "Background Depth", 0, 40, 12, 0, "dB");
    p.f (ids::bg_release, "Background Release", 20, 1000, 200, 200, "ms", 0);
    p.f (ids::bg_range, "Background Range", 10, 70, 40, 0, "dB");
    p.f (ids::bg_amount, "Background Amount", 0, 100, 40, 0, "%");
    // VOICE ISOLATE
    p.b (ids::vi_on, "Isolate On", false);
    p.f (ids::vi_amount, "Isolate Amount", 0, 100, 50, 0, "%");
    p.f (ids::vi_low, "Isolate Low", 40, 400, 80, 100, "Hz");
    p.f (ids::vi_high, "Isolate High", 3000, 20000, 14000, 9000, "Hz");
    p.f (ids::vi_focus, "Isolate Focus", 0, 100, 40, 0, "%");
    // BREATH
    p.b (ids::br_on, "Breath On", false);
    p.f (ids::br_thresh, "Breath Threshold", 6, 40, 20, 0, "dB");
    p.f (ids::br_reduce, "Breath Reduction", 0, 30, 8, 0, "dB");
    // DE-VERB
    p.b (ids::dv_on, "De-Verb On", false);
    p.f (ids::dv_amount, "De-Verb Amount", 0, 100, 50, 0, "%");
    p.f (ids::dv_decay, "Room Decay", 0.1f, 3.0f, 0.7f, 0.7f, "s", 2);
    p.f (ids::dv_early, "Early Keep", 5, 120, 40, 40, "ms", 0);
    p.f (ids::dv_floor, "De-Verb Floor", 0, 30, 15, 0, "dB");
    // ARTIFACT CONTROL
    p.f (ids::sp_smooth, "Smoothing", 0, 100, 50, 0, "%");
    // DE-ESS
    p.b (ids::de_on, "De-Ess On", true);
    p.f (ids::de_freq, "De-Ess Freq", 2000, 14000, 5500, 5500, "Hz");
    p.f (ids::de_thresh, "De-Ess Threshold", -60, 0, -28, 0, "dB");
    p.f (ids::de_range, "De-Ess Range", 0, 24, 8, 0, "dB");
    p.f (ids::de_release, "De-Ess Release", 10, 250, 60, 60, "ms", 0);
    p.c (ids::de_mode, "De-Ess Mode", { "Split", "Wide" }, 0);
    p.b (ids::de_listen, "De-Ess Listen", false);
    // FET
    p.b (ids::c_on, "FET On", true);
    p.f (ids::c_thresh, "FET Threshold", -60, 0, -18, 0, "dB");
    p.f (ids::c_ratio, "FET Ratio", 1, 20, 4, 4, ":1");
    p.f (ids::c_knee, "FET Knee", 0, 18, 3, 0, "dB");
    p.f (ids::c_attack, "FET Attack", 0.02f, 50, 0.8f, 2.0f, "ms", 2);
    p.f (ids::c_release, "FET Release", 20, 1200, 120, 150, "ms", 0);
    p.f (ids::c_makeup, "FET Makeup", -12, 24, 0, 0, "dB");
    p.b (ids::c_auto, "FET Auto Makeup", true);
    p.b (ids::c_allbtn, "FET All Buttons", false);
    p.f (ids::c_mix, "FET Mix", 0, 100, 100, 0, "%");
    p.f (ids::c_schpf, "FET SC HPF", 20, 500, 90, 100, "Hz");
    // OPTO (LA-2A style)
    p.b (ids::op_on, "Opto On", false);
    p.f (ids::op_peak, "Opto Peak Reduction", 0, 100, 40, 0, "%");
    p.f (ids::op_gain, "Opto Gain", -12, 30, 0, 0, "dB");
    p.c (ids::op_mode, "Opto Mode", { "Compress", "Limit" }, 0);
    p.f (ids::op_emph, "Opto HF Emphasis", 0, 100, 30, 0, "%");
    p.f (ids::op_mix, "Opto Mix", 0, 100, 100, 0, "%");
    // EQ
    p.b (ids::eq_on, "EQ On", true);
    const float defF[8] { 40, 120, 300, 800, 2500, 5000, 10000, 18000 };
    const int defT[8] { 3, 1, 0, 0, 0, 0, 2, 4 };
    const float defQ[8] { 0.71f, 0.71f, 1.2f, 1.0f, 1.0f, 1.4f, 0.71f, 0.71f };
    for (int b = 0; b < 8; ++b)
    {
        const String n = "EQ " + String (b + 1) + " ";
        p.b (eqId (b, "on"), n + "On", false);
        p.c (eqId (b, "type"), n + "Type", eqTypeNames(), defT[b]);
        p.f (eqId (b, "freq"), n + "Freq", 20, 20000, defF[b], 1000, "Hz");
        p.f (eqId (b, "gain"), n + "Gain", -24, 24, 0, 0, "dB");
        p.f (eqId (b, "q"), n + "Q", 0.1f, 18, defQ[b], 1.0f, "", 2);
        p.b (eqId (b, "dyn"), n + "Dynamic", false);
        p.f (eqId (b, "thresh"), n + "Threshold", -60, 0, -30, 0, "dB");
        p.f (eqId (b, "ratio"), n + "Ratio", 1, 10, 3, 3, ":1");
        p.f (eqId (b, "range"), n + "Range", -24, 24, -6, 0, "dB");
        p.f (eqId (b, "attack"), n + "Attack", 0.1f, 100, 5, 10, "ms", 1);
        p.f (eqId (b, "release"), n + "Release", 5, 1000, 80, 100, "ms", 0);
    }
    // OUTPUT
    p.f (ids::out_gain, "Output Gain", -24, 24, 0, 0, "dB");
    p.f (ids::mix, "Mix", 0, 100, 100, 0, "%");
    p.b (ids::delta, "Delta (Listen Removed)", false);
    return std::move (p.layout);
}
