#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ids
{
#define VR_ID(name) inline constexpr const char* name = #name;
// input
VR_ID (in_gain) VR_ID (hpf_on) VR_ID (hpf_freq) VR_ID (hpf_slope)
// de-hum
VR_ID (hum_on) VR_ID (hum_freq) VR_ID (hum_harm) VR_ID (hum_q) VR_ID (hum_depth)
// de-click
VR_ID (dc_on) VR_ID (dc_sens) VR_ID (dc_width) VR_ID (dc_focus) VR_ID (dc_depth)
// de-plosive
VR_ID (dp_on) VR_ID (dp_sens) VR_ID (dp_freq) VR_ID (dp_depth) VR_ID (dp_release)
// de-noise
VR_ID (dn_on) VR_ID (dn_amount) VR_ID (dn_reduction) VR_ID (dn_tilt) VR_ID (dn_mode) VR_ID (dn_learn)
// background
VR_ID (bg_on) VR_ID (bg_thresh) VR_ID (bg_depth) VR_ID (bg_release) VR_ID (bg_range) VR_ID (bg_amount)
// voice isolate
VR_ID (vi_on) VR_ID (vi_amount) VR_ID (vi_low) VR_ID (vi_high) VR_ID (vi_focus)
// breath
VR_ID (br_on) VR_ID (br_thresh) VR_ID (br_reduce)
// de-verb
VR_ID (dv_on) VR_ID (dv_amount) VR_ID (dv_decay) VR_ID (dv_early) VR_ID (dv_floor)
// artifact control
VR_ID (sp_smooth)
// de-ess
VR_ID (de_on) VR_ID (de_freq) VR_ID (de_thresh) VR_ID (de_range) VR_ID (de_release) VR_ID (de_mode) VR_ID (de_listen)
// FET compressor
VR_ID (c_on) VR_ID (c_thresh) VR_ID (c_ratio) VR_ID (c_knee) VR_ID (c_attack) VR_ID (c_release)
VR_ID (c_makeup) VR_ID (c_auto) VR_ID (c_allbtn) VR_ID (c_mix) VR_ID (c_schpf)
// opto compressor
VR_ID (op_on) VR_ID (op_peak) VR_ID (op_gain) VR_ID (op_mode) VR_ID (op_emph) VR_ID (op_mix)
// eq + output
VR_ID (eq_on)
VR_ID (out_gain) VR_ID (mix) VR_ID (delta)
#undef VR_ID
} // namespace ids

// EQ band fields: on type freq gain q dyn thresh ratio range attack release
inline juce::String eqId (int band, const char* field) { return "eq" + juce::String (band + 1) + "_" + field; }

inline const juce::StringArray& eqTypeNames()
{
    static const juce::StringArray names { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
