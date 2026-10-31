#pragma once
// Glue / console stage — last in the chain. Four characters:
//   Tape  : vintage tape — emphasis-wrapped soft saturation (HF saturates first), slight
//           asymmetry, head bump, HF roll-off, slow gentle compression
//   Tube  : Siemens-style tube console — triode-like asymmetric curve (2nd harmonic),
//           warm low shelf, softened top, slow smooth levelling
//   API   : API-style — op-amp curve with a firmer knee (odd harmonics), low-mid punch and
//           forward upper mids, fast punchy compression
//   Neve  : Neve-style — transformer saturation weighted toward the lows (3rd harmonic,
//           thick bottom), silky air shelf, programme-dependent release
// Saturation runs 4x oversampled (linear-phase, integer latency). Every curve has unity
// small-signal gain, so Drive adds harmonics/density without a big level jump.
#include "DspCore.h"

namespace vr
{
class Glue
{
public:
    enum Mode { Tape = 0, Tube, Api, Neve };
    static constexpr double kOS = 4.0;     // oversampling factor for the saturation
    struct Settings { bool on = false; int mode = Tape; float drive01 = 0.3f, color01 = 0.5f, glue01 = 0.3f, mix01 = 1.0f, outDb = 0.0f; };

    void prepare (double sr, int maxCh, int maxBlock)
    {
        fs = sr; nchMax = maxCh;
        os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) maxCh, 2,     // 4x
                juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os->initProcessing ((size_t) maxBlock);
        latency = (int) std::lround (os->getLatencyInSamples());
        R = nextPow2 (latency + maxBlock + 8); mask = R - 1;
        ring.assign ((size_t) maxCh, std::vector<float> ((size_t) R, 0.0f));
        dry.assign ((size_t) maxCh, std::vector<float> ((size_t) maxBlock, 0.0f));
        st.assign ((size_t) maxCh, {});
        pos = 0; env = 0; grDb = 0; grSlow = 0; wasOn = false; lastKey = -1;
    }
    int getLatency() const { return latency; }

    // tone filters actually applied after the saturator (also used to draw the curve)
    static std::vector<SvfCoefs> toneFilters (int mode, float c, double fsOS)
    {
        std::vector<SvfCoefs> f;
        switch (mode)
        {
            case Tape:
                f.push_back (makeSvf (Bell, fsOS, 75.0, 3.5 * c, 1.0));                    // head bump
                f.push_back (makeSvf (Bell, fsOS, 32.0, -2.0 * c, 1.2));                   // bump's lower dip
                f.push_back (makeSvf (HighCut, fsOS, 22000.0 - 10000.0 * c, 0.0, 0.6));        // tape HF loss
                break;
            case Tube:
                f.push_back (makeSvf (LowShelf, fsOS, 110.0, 2.5 * c, 0.7));
                f.push_back (makeSvf (Bell, fsOS, 700.0, 0.8 * c, 0.6));
                f.push_back (makeSvf (HighShelf, fsOS, 9000.0, -2.5 * c, 0.6));
                break;
            case Api:
                f.push_back (makeSvf (LowCut, fsOS, 22.0, 0.0, 0.7));
                f.push_back (makeSvf (Bell, fsOS, 200.0, 2.0 * c, 0.9));                   // punch
                f.push_back (makeSvf (Bell, fsOS, 3200.0, 1.8 * c, 0.8));                  // forward mids
                break;
            case Neve:
            default:
                f.push_back (makeSvf (LowShelf, fsOS, 70.0, 2.8 * c, 0.7));                // thick bottom
                f.push_back (makeSvf (Bell, fsOS, 1600.0, 0.8 * c, 0.7));
                f.push_back (makeSvf (HighShelf, fsOS, 12000.0, 2.2 * c, 0.6));            // silky air
                break;
        }
        return f;
    }

    // static saturation curve for each character (unity slope at 0)
    static inline float shape (int mode, float x) noexcept
    {
        switch (mode)
        {
            case Tape:
            {
                constexpr float b = 0.06f;
                const float t = std::tanh (b);
                return (std::tanh (x + b) - t) / (1.0f - t * t);
            }
            case Tube:
            {
                constexpr float b = 0.35f;
                const float t = std::tanh (b);
                return (std::tanh (x + b) - t) / (1.0f - t * t);
            }
            case Api:
            {
                const float a = std::abs (x);
                return x / std::cbrt (1.0f + a * a * a);
            }
            case Neve:
            default:
            {
                constexpr float b = 0.12f;      // class-A stage: a little 2nd on top of the transformer's 3rd
                const float t = std::tanh (b);
                return (std::tanh (x + b) - t) / (1.0f - t * t);
            }
        }
    }

    void process (float* const* ch, int nch, int n, const Settings& s)
    {
        if (! s.on)
        {
            // keep latency constant: pure delay
            for (int c = 0; c < nch; ++c)
            {
                auto& rg = ring[(size_t) c];
                for (int i = 0; i < n; ++i)
                {
                    const int w = (pos + i) & mask;
                    rg[(size_t) w] = ch[c][i];
                    ch[c][i] = rg[(size_t) ((w - latency) & mask)];
                }
            }
            pos = (pos + n) & mask;
            wasOn = false;
            reduction.store (0.0f); sideDb.store (-100.0f);
            return;
        }
        if (! wasOn) { os->reset(); for (auto& c : st) c = {}; wasOn = true; }
        const double fsOS = fs * kOS;
        const int key = s.mode * 100000 + (int) (s.color01 * 1000.0f);
        if (key != lastKey)
        {
            tone = toneFilters (s.mode, s.color01, fsOS);
            emphUp = makeSvf (HighShelf, fsOS, 4500.0, 6.0, 0.6);
            emphDown = makeSvf (HighShelf, fsOS, 4500.0, -6.0, 0.6);
            neveLow = makeSvf (HighCut, fsOS, 180.0, 0.0, 0.6);
            lastKey = key;
        }

        // ---- 1. dry copy (delayed to match oversampling latency) for the Mix control
        for (int c = 0; c < nch; ++c)
        {
            auto& rg = ring[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                const int w = (pos + i) & mask;
                rg[(size_t) w] = ch[c][i];
                dry[(size_t) c][(size_t) i] = rg[(size_t) ((w - latency) & mask)];
            }
        }
        pos = (pos + n) & mask;

        // ---- 2. bus compression ("glue") at base rate, character-dependent ballistics
        float ratio = 2, attMs = 20, relMs = 250, knee = 10;
        switch (s.mode)
        {
            case Tape: ratio = 1.6f; attMs = 30; relMs = 300; knee = 12; break;
            case Tube: ratio = 2.0f; attMs = 25; relMs = 450; knee = 12; break;
            case Api:  ratio = 4.0f; attMs = 6;  relMs = 120; knee = 4;  break;
            case Neve: ratio = 2.5f; attMs = 15; relMs = 200; knee = 8;  break;
            default: break;
        }
        float maxRed = 0, maxLvl = -120;
        if (s.glue01 > 0.001f)
        {
            const float thresh = -4.0f - 26.0f * s.glue01;
            const float over10 = -10.0f - thresh;
            const float makeup = over10 > 0 ? 0.4f * over10 * (1.0f - 1.0f / ratio) : 0.0f;
            const float att = msCoef (attMs, fs), detC = msCoef (8.0f, fs);
            for (int i = 0; i < n; ++i)
            {
                float l = 0;
                for (int c = 0; c < nch; ++c) l = std::max (l, ch[c][i] * ch[c][i]);
                env = detC * env + (1 - detC) * l;
                const float xg = 10.0f * std::log10 (env * 2.0f + 1.0e-12f);
                maxLvl = std::max (maxLvl, xg);
                const float d = xg - thresh;
                float tgt = 0;
                if (2 * d >= -knee)
                    tgt = (2 * std::abs (d) <= knee) ? (1.0f - 1.0f / ratio) * (d + knee / 2) * (d + knee / 2) / (2 * knee)
                                                     : d * (1.0f - 1.0f / ratio);
                // Neve-style auto release: longer after sustained compression
                float rMs = relMs;
                if (s.mode == Neve) { grSlow = 0.9995f * grSlow + 0.0005f * tgt; rMs = relMs + 600.0f * std::min (1.0f, grSlow / 6.0f); }
                const float rel = msCoef (rMs, fs);
                grDb = tgt > grDb ? att * grDb + (1 - att) * tgt : rel * grDb + (1 - rel) * tgt;
                maxRed = std::max (maxRed, grDb);
                const float gg = dbToGain (makeup - grDb);
                for (int c = 0; c < nch; ++c) ch[c][i] *= gg;
            }
        }
        reduction.store (maxRed);
        sideDb.store (maxLvl);

        // ---- 3. oversampled colour: pre -> saturate -> tone
        driveS = driveS < 0 ? s.drive01 : driveS + (s.drive01 - driveS) * std::min (1.0f, (float) n / (0.02f * (float) fs));
        const float drv = dbToGain (driveS * 24.0f);
        const float inv = 1.0f / drv;
        juce::dsp::AudioBlock<float> block (ch, (size_t) nch, (size_t) n);
        auto up = os->processSamplesUp (block);
        const int m = (int) up.getNumSamples();
        for (int c = 0; c < nch; ++c)
        {
            float* x = up.getChannelPointer ((size_t) c);
            auto& S = st[(size_t) c];
            for (int i = 0; i < m; ++i)
            {
                float v = x[i];
                switch (s.mode)
                {
                    case Tape:
                        v = S.e1.tick (emphUp, v);
                        v = shape (Tape, v * drv) * inv;
                        v = S.e2.tick (emphDown, v);
                        break;
                    case Neve:
                    {
                        const float lo = S.lf.tick (neveLow, v);
                        constexpr float k = 0.7f;      // transformer: lows drive the core harder
                        v = shape (Neve, (v + k * lo) * drv) * inv - k * lo;
                        break;
                    }
                    default:
                        v = shape (s.mode, v * drv) * inv;
                        break;
                }
                for (size_t f = 0; f < tone.size(); ++f) v = S.t[f].tick (tone[f], v);
                // DC blocker (asymmetric curves)
                const float y = v - S.dcX + 0.9995f * S.dcY;
                S.dcX = v; S.dcY = y;
                x[i] = y;
            }
        }
        os->processSamplesDown (block);

        // ---- 4. mix + output (smoothed, zipper-free)
        const float sm = msCoef (20.0f, fs);
        const float ogT = dbToGain (s.outDb);
        if (! started) { ogS = ogT; mixS = s.mix01; started = true; }
        for (int i = 0; i < n; ++i)
        {
            ogS = glideTo (ogS, ogT, sm);
            mixS = glideTo (mixS, s.mix01, sm);
            for (int c = 0; c < nch; ++c)
                ch[c][i] = ogS * (mixS * ch[c][i] + (1.0f - mixS) * dry[(size_t) c][(size_t) i]);
        }
    }

    std::atomic<float> reduction { 0.0f }, sideDb { -100.0f };

private:
    struct Chan { SvfState e1, e2, lf; std::array<SvfState, 4> t {}; float dcX = 0, dcY = 0; };
    double fs = 48000; int nchMax = 2, latency = 0, R = 1024, mask = 1023, pos = 0, lastKey = -1;
    float env = 0, grDb = 0, grSlow = 0, ogS = 1, mixS = 1, driveS = -1; bool wasOn = false, started = false;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::vector<std::vector<float>> ring, dry;
    std::vector<Chan> st;
    std::vector<SvfCoefs> tone;
    SvfCoefs emphUp, emphDown, neveLow;
};
} // namespace vr
