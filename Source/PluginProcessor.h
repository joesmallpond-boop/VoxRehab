#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <unordered_map>
#include "Params.h"
#include "DSP/DspCore.h"
#include "DSP/DynamicEq.h"
#include "DSP/SpectralRepair.h"
#include "DSP/Glue.h"

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
    enum Module { Overall = 0, Hum, Click, Plosive, Noise, Background, Isolator, Breath, Verb, DeEss, Fet, Opto, Eq, Glue, kNum };
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
    // Re-orderable stages (Input is always first, Output always last)
    enum Stage { StHum = 0, StClick, StPlosive, StSpectral, StDeEss, StFet, StOpto, StEq, StGlue, kStages };
    using Order = std::array<int, kStages>;
    static Order defaultOrder() { return { StHum, StClick, StPlosive, StSpectral, StDeEss, StFet, StOpto, StEq, StGlue }; }
    static const char* stageName (int s);
    Order getOrder() const { return unpack (packedOrder.load()); }
    void setOrder (const Order& o) { if (isValid (o)) packedOrder.store (pack (o)); }
    static bool isValid (const Order& o)
    {
        std::array<bool, kStages> seen {};
        for (int v : o) { if (v < 0 || v >= kStages || seen[(size_t) v]) return false; seen[(size_t) v] = true; }
        return true;
    }

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
    vr::Glue glue;

    ScopeBuffer scopeIn, scopeOut;
    Monitor monitor;
    std::atomic<float> inPeak { -100.0f }, outPeak { -100.0f };
    double getCurrentSampleRate() const { return currentFs; }
    bool hasLearnedProfile() const { return spectral.learnedValid; }

private:
    static uint64_t pack (const Order& o) { uint64_t v = 0; for (int i = 0; i < kStages; ++i) v |= (uint64_t) o[(size_t) i] << (4 * i); return v; }
    static Order unpack (uint64_t v) { Order o {}; for (int i = 0; i < kStages; ++i) o[(size_t) i] = (int) ((v >> (4 * i)) & 15); return o; }
    std::atomic<uint64_t> packedOrder { pack (defaultOrder()) };

    void processChunk (float* const* ch, int nch, int n);
    void commitTick();

    // monitor accumulation (audio thread only)
    int tickSamples = 128, tickPos = 0, lastClicks = 0;
    float lastPlosGr = 0;
    std::array<double, Monitor::kNum> accPre {}, accPost {};
    std::array<float, Monitor::kNum> accGr {}, accSide {}, accSide2 {};
    std::array<bool, Monitor::kNum> accEv {};
    double accCount = 0;
    // Parameter access on the audio thread: pointers resolved once in the constructor, so a read is a
    // pointer-keyed hash lookup + atomic load (no string compares, no allocation).
    std::unordered_map<const void*, std::atomic<float>*> paramPtr;
    std::array<std::array<std::atomic<float>*, kEqFields>, 8> eqPtr {};
    float pv (const char* id) const { return paramPtr.at (id)->load (std::memory_order_relaxed); }
    float eqv (int band, int field) const { return eqPtr[(size_t) band][(size_t) field]->load (std::memory_order_relaxed); }

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
