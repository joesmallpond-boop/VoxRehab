#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#define VR_IDS(X) \
    X (in_gain) \
    X (hpf_on) \
    X (hpf_freq) \
    X (hpf_slope) \
    X (hum_on) \
    X (hum_freq) \
    X (hum_harm) \
    X (hum_q) \
    X (hum_depth) \
    X (dc_on) \
    X (dc_sens) \
    X (dc_width) \
    X (dc_focus) \
    X (dc_depth) \
    X (dp_on) \
    X (dp_sens) \
    X (dp_freq) \
    X (dp_depth) \
    X (dp_release) \
    X (dp_mode) \
    X (dp_width) \
    X (dn_on) \
    X (dn_amount) \
    X (dn_reduction) \
    X (dn_tilt) \
    X (dn_mode) \
    X (dn_learn) \
    X (bg_on) \
    X (bg_thresh) \
    X (bg_depth) \
    X (bg_release) \
    X (bg_range) \
    X (bg_amount) \
    X (vi_on) \
    X (vi_amount) \
    X (vi_low) \
    X (vi_high) \
    X (vi_focus) \
    X (br_on) \
    X (br_thresh) \
    X (br_reduce) \
    X (dv_on) \
    X (dv_amount) \
    X (dv_decay) \
    X (dv_early) \
    X (dv_floor) \
    X (sp_smooth) \
    X (de_on) \
    X (de_freq) \
    X (de_thresh) \
    X (de_range) \
    X (de_release) \
    X (de_mode) \
    X (de_listen) \
    X (c_on) \
    X (c_thresh) \
    X (c_ratio) \
    X (c_knee) \
    X (c_attack) \
    X (c_release) \
    X (c_makeup) \
    X (c_auto) \
    X (c_allbtn) \
    X (c_mix) \
    X (c_schpf) \
    X (op_on) \
    X (op_peak) \
    X (op_gain) \
    X (op_mode) \
    X (op_emph) \
    X (op_mix) \
    X (gl_on) \
    X (gl_mode) \
    X (gl_drive) \
    X (gl_color) \
    X (gl_glue) \
    X (gl_mix) \
    X (gl_out) \
    X (eq_on) \
    X (out_gain) \
    X (mix) \
    X (delta)

namespace ids
{
#define VR_DECL(name) inline constexpr const char* name = #name;
VR_IDS (VR_DECL)
#undef VR_DECL
#define VR_ENTRY(name) name,
inline constexpr const char* all[] = { VR_IDS (VR_ENTRY) };
#undef VR_ENTRY
} // namespace ids

// EQ band fields, in order
inline constexpr const char* eqFields[] = { "on", "type", "freq", "gain", "q", "dyn", "thresh", "ratio", "range", "attack", "release" };
enum EqField { EqOn = 0, EqType, EqFreq, EqGain, EqQ, EqDyn, EqThresh, EqRatio, EqRange, EqAttack, EqRelease, kEqFields };

// EQ band fields: on type freq gain q dyn thresh ratio range attack release
inline juce::String eqId (int band, const char* field) { return "eq" + juce::String (band + 1) + "_" + field; }

inline const juce::StringArray& eqTypeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
