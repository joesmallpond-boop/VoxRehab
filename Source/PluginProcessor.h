#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "DSP/DspCore.h"
#include "DSP/DynamicEq.h"
#include "DSP/SpectralRepair.h"

// Lock-free-ish scope buffer for the analyser (tearing is harmless for display)
struct ScopeBuffer
{
    static constexpr int kSize = 16384;
    std::array<float, kSize> data {};
    std::atomic<int> writePos { 0 };
    void push (const float* x, int n)
    {
        int w = writePos.load();
        for (int i = 0; i < n; ++i) { data[(size_t) w] = x[i]; w = (w + 1) & (kSize - 1); }
        writePos.store (w);
    }
    void copyLatest (float* dst, int n) const
    {
        int r = (writePos.load() - n) & (kSize - 1);
        for (int i = 0; i < n; ++i) { dst[i] = data[(size_t) r]; r = (r + 1) & (kSize - 1); }
    }
};

// Per-module history for the Process Monitor. Written by the audio thread once per
// "tick" (~2.7 ms), read by the UI. Values are levels / reductions in dB.
struct Monitor
{
    enum Module { Overall = 0, Hum, Click, Plosive, Noise, Background, Isolator, Breath, Verb, DeEss, Fet, Opto, Eq, kNum };
    static constexpr int kLen = 4096;      // ~11 s of history
    struct Track
    {
        std::array<float, kLen> pre {}, post {}, gr {}, side {}, side2 {};
        std::array<uint8_t, kLen> ev {};
        std::atomic<float> latencyTicks { 0.0f };
    };
    std::array<Track, kNum> tr;
    std::atomic<int> tick { 0 };
    std::atomic<float> tickSeconds { 128.0f / 48000.0f };
};

class VoxRehabProcessor : public juce::AudioProcessor
{
public:
    VoxRehabProcessor();
    ~VoxRehabProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Vox Rehab"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // DSP (public so the editor can read meters)
    vr::InputStage input;
    vr::DeHum deHum;
    vr::DeClick deClick;
    vr::DePlosive dePlosive;
    vr::SpectralRepair spectral;
    vr::DeEsser deEsser;
    vr::FetCompressor fet;
    vr::OptoCompressor opto;
    vr::DynamicEq eq;

    ScopeBuffer scopeIn, scopeOut;
    Monitor monitor;
    std::atomic<float> inPeak { -100.0f }, outPeak { -100.0f };
    double getCurrentSampleRate() const { return currentFs; }
    bool hasLearnedProfile() const { return spectral.learnedValid; }

private:
    void processChunk (float* const* ch, int nch, int n);
    void commitTick();

    // monitor accumulation (audio thread only)
    int tickSamples = 128, tickPos = 0, lastClicks = 0;
    float lastPlosGr = 0;
    std::array<double, Monitor::kNum> accPre {}, accPost {};
    std::array<float, Monitor::kNum> accGr {}, accSide {}, accSide2 {};
    std::array<bool, Monitor::kNum> accEv {};
    double accCount = 0;
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    float pv (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    double currentFs = 48000;
    int maxChunk = 512, latency = 0, currentProgram = 0;
    std::vector<std::vector<float>> dryRing;
    std::vector<std::vector<float>> dryBuf;
    int dryPos = 0, dryMask = 0;
    std::vector<float> monoTmp;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outGain;
    juce::SmoothedValue<float> mixS;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoxRehabProcessor)
};
