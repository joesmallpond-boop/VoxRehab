#pragma once
// 8-band parametric EQ, every band optionally dynamic. Bands are zero-delay-feedback
// state-variable filters (Simper/Cytomic TPT), which stay clean while their gain is
// being modulated by the band's own detector.
#include "DspCore.h"

namespace vr
{
class DynamicEq
{
public:
    static constexpr int kBands = 8;
    struct Band
    {
        bool on = false; int type = Bell; float freq = 1000, gain = 0, q = 1;
        bool dyn = false; float thresh = -30, ratio = 2, range = -6, attack = 5, release = 80;
    };

    void prepare (double sr, int maxCh)
    {
        fs = sr;
        for (int b = 0; b < kBands; ++b)
        {
            st[(size_t) b].assign ((size_t) maxCh, {});
            det[(size_t) b].assign ((size_t) maxCh, {});
            env[(size_t) b] = 0; offset[(size_t) b] = 0;
            liveGain[(size_t) b].store (0.0f);
        }
    }

    // dynamic gain offset for a given detector level (dB)
    static float dynOffset (const Band& b, float levelDb)
    {
        const float over = levelDb - b.thresh;
        if (over <= 0) return 0;
        const float red = over * (1.0f - 1.0f / std::max (1.0f, b.ratio));
        return b.range < 0 ? -std::min (red, -b.range) : std::min (red, b.range);
    }

    void process (float* const* data, int nch, int n, const std::array<Band, kBands>& bands, bool enabled)
    {
        if (! enabled)
        {
            for (int b = 0; b < kBands; ++b) liveGain[(size_t) b].store (bands[(size_t) b].gain);
            return;
        }
        constexpr int sub = 16;
        for (int start = 0; start < n; start += sub)
        {
            const int len = std::min (sub, n - start);
            for (int b = 0; b < kBands; ++b)
            {
                const auto& bd = bands[(size_t) b];
                if (! bd.on) continue;
                const bool canDyn = bd.dyn && (bd.type == Bell || bd.type == LowShelf || bd.type == HighShelf);
                if (! canDyn) offset[(size_t) b] = 0;
                const float effGain = bd.gain + offset[(size_t) b];
                const auto c = makeSvf (bd.type, fs, bd.freq, effGain, bd.q);
                liveGain[(size_t) b].store (effGain);
                if (canDyn)
                {
                    const auto dc = makeBandpassDetector (fs, bd.freq, std::max (0.5f, bd.q));
                    const float at = msCoef (bd.attack, fs), rl = msCoef (bd.release, fs);
                    float e = env[(size_t) b];
                    for (int i = start; i < start + len; ++i)
                    {
                        float lvl = 0;
                        for (int ch = 0; ch < nch; ++ch)
                        {
                            const float x = data[ch][i];
                            lvl = std::max (lvl, std::abs (det[(size_t) b][(size_t) ch].tick (dc, x)));
                            data[ch][i] = st[(size_t) b][(size_t) ch].tick (c, x);
                        }
                        e = lvl > e ? at * e + (1 - at) * lvl : rl * e + (1 - rl) * lvl;
                    }
                    env[(size_t) b] = e;
                    offset[(size_t) b] = dynOffset (bd, gainToDb (e));
                }
                else
                {
                    for (int ch = 0; ch < nch; ++ch)
                    {
                        auto& s = st[(size_t) b][(size_t) ch];
                        for (int i = start; i < start + len; ++i) data[ch][i] = s.tick (c, data[ch][i]);
                    }
                }
            }
        }
        for (int b = 0; b < kBands; ++b)
            if (! bands[(size_t) b].on) liveGain[(size_t) b].store (bands[(size_t) b].gain);
    }

    std::array<std::atomic<float>, kBands> liveGain {};

private:
    double fs = 48000;
    std::array<std::vector<SvfState>, kBands> st, det;
    std::array<float, kBands> env {}, offset {};
};
} // namespace vr
