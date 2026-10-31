#pragma once
// Vox Rehab — core DSP helpers and the time-domain repair / dynamics stages.
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>
#include <array>
#include <cmath>
#include <complex>
#include <algorithm>

namespace vr
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float msCoef (float ms, double fs) noexcept
{
    return ms <= 0.0f ? 0.0f : (float) std::exp (-1.0 / ((double) ms * 0.001 * fs));
}
inline int nextPow2 (int v) { int p = 1; while (p < v) p <<= 1; return p; }

//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }
    inline float process (float xIn) noexcept
    {
        const double x = xIn;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
    {
        b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0;
    }
    static double clampF (double fs, double f) { return std::clamp (f, 5.0, 0.48 * fs); }
    void setLowpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    // band-pass with 0 dB peak gain
    void setBandpass (double fs, double f, double q)
    {
        const double w = 2 * kPi * clampF (fs, f) / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void setHighShelf (double fs, double f, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * clampF (fs, f) / fs;
        const double c = std::cos (w), s = std::sin (w), al = s / 2 * std::sqrt (2.0);
        const double sa = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};

//==============================================================================
// Zero-delay-feedback state-variable filter (Simper/Cytomic). Clean under modulation;
// a shelf or bell at 0 dB is an exact identity (m0=1, m1=m2=0).
enum EqType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch };

struct SvfCoefs { float g = 0, k = 1, a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0; };

inline SvfCoefs makeSvf (int type, double fs, double f, double gainDb, double q)
{
    f = std::clamp (f, 10.0, 0.49 * fs);
    q = std::max (q, 0.05);
    const double A = std::pow (10.0, gainDb / 40.0);
    double g = std::tan (kPi * f / fs), k = 1.0 / q, m0 = 1, m1 = 0, m2 = 0;
    switch (type)
    {
        case Bell:      k = 1.0 / (q * A); m0 = 1; m1 = k * (A * A - 1); m2 = 0; break;
        case LowShelf:  g /= std::sqrt (A); m0 = 1; m1 = k * (A - 1); m2 = A * A - 1; break;
        case HighShelf: g *= std::sqrt (A); m0 = A * A; m1 = k * (1 - A) * A; m2 = 1 - A * A; break;
        case LowCut:    m0 = 1; m1 = -k; m2 = -1; break;
        case HighCut:   m0 = 0; m1 = 0; m2 = 1; break;
        case Notch:     m0 = 1; m1 = -k; m2 = 0; break;
        default: break;
    }
    SvfCoefs c;
    c.g = (float) g; c.k = (float) k;
    c.a1 = (float) (1.0 / (1.0 + g * (g + k)));
    c.a2 = (float) (g * c.a1);
    c.a3 = (float) (g * c.a2);
    c.m0 = (float) m0; c.m1 = (float) m1; c.m2 = (float) m2;
    return c;
}

inline SvfCoefs makeBandpassDetector (double fs, double f, double q)
{
    auto c = makeSvf (Bell, fs, f, 0.0, q);
    c.m0 = 0; c.m1 = c.k; c.m2 = 0;   // unity-peak band-pass
    return c;
}

// exact magnitude of the digital SVF at frequency f (for drawing)
inline double svfMagnitude (const SvfCoefs& c, double fs, double f)
{
    const double w = std::tan (kPi * std::min (f, 0.4999 * fs) / fs) / (double) c.g;
    const std::complex<double> s (0.0, w);
    const auto D = s * s + (double) c.k * s + 1.0;
    const auto H = (double) c.m0 + (double) c.m1 * s / D + (double) c.m2 / D;
    return std::abs (H);
}

struct SvfState
{
    float ic1 = 0, ic2 = 0;
    inline float tick (const SvfCoefs& c, float v0) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
};

//==============================================================================
// Input trim + high-pass
class InputStage
{
public:
    void prepare (double sr, int maxCh)
    {
        fs = sr;
        f1.assign ((size_t) maxCh, {});
        f2.assign ((size_t) maxCh, {});
        gain.reset (sr, 0.02);
        gain.setCurrentAndTargetValue (1.0f);
        lastF = -1.0f;
    }

    void process (float* const* ch, int nch, int n, float gainDb, bool hpOn, float hpHz, bool steep)
    {
        gain.setTargetValue (dbToGain (gainDb));
        if (hpHz != lastF || steep != lastSteep)
        {
            for (int c = 0; c < nch; ++c)
            {
                f1[(size_t) c].setHighpass (fs, hpHz, steep ? 0.54119610 : 0.70710678);
                f2[(size_t) c].setHighpass (fs, hpHz, 1.30656296);
            }
            lastF = hpHz; lastSteep = steep;
        }
        for (int i = 0; i < n; ++i)
        {
            const float g = gain.getNextValue();
            for (int c = 0; c < nch; ++c)
            {
                float x = ch[c][i] * g;
                if (hpOn)
                {
                    x = f1[(size_t) c].process (x);
                    if (steep) x = f2[(size_t) c].process (x);
                }
                ch[c][i] = x;
            }
        }
    }

private:
    double fs = 48000;
    std::vector<Biquad> f1, f2;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> gain;
    float lastF = -1; bool lastSteep = false;
};

//==============================================================================
// Mains hum remover: cascade of narrow notches on the fundamental + harmonics.
class DeHum
{
public:
    static constexpr int kMaxH = 12;
    void prepare (double sr, int maxCh)
    {
        fs = sr;
        f.assign ((size_t) maxCh, {});
        lastBase = -1;
    }

    void process (float* const* ch, int nch, int n, bool on, float base, int harmonics, float q, float depthDb)
    {
        reduction.store (on ? depthDb : 0.0f);
        if (! on) return;
        if (base != lastBase || harmonics != lastH || q != lastQ)
        {
            for (auto& arr : f)
                for (int h = 0; h < kMaxH; ++h)
                {
                    const double fc = base * (h + 1);
                    active[(size_t) h] = (h < harmonics) && fc < 0.45 * fs;
                    if (active[(size_t) h]) arr[(size_t) h].setBandpass (fs, fc, q * (h + 1));
                }
            lastBase = base; lastH = harmonics; lastQ = q;
        }
        const float amt = 1.0f - dbToGain (-depthDb);
        for (int c = 0; c < nch; ++c)
        {
            auto& arr = f[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                float x = ch[c][i];
                for (int h = 0; h < kMaxH; ++h)
                    if (active[(size_t) h]) x -= amt * arr[(size_t) h].process (x);
                ch[c][i] = x;
            }
        }
    }

    std::atomic<float> reduction { 0.0f };

private:
    double fs = 48000;
    std::vector<std::array<Biquad, kMaxH>> f;
    std::array<bool, kMaxH> active {};
    float lastBase = -1, lastQ = -1; int lastH = -1;
};

//==============================================================================
// Mouth-click / crackle remover. The signal is split into body (low) and click band
// (high = x - low, so the split is perfectly complementary). A click is a spike in the
// second difference of the click band that towers over the loudest peak of the previous
// ~20 ms (longer than any voice period, so glottal pulses never qualify) AND over the
// next few ms (lookahead, so consonant onsets are left alone). Repair = a short, faded
// dip of the click band only; the body of the voice is untouched.
class DeClick
{
public:
    void prepare (double sr, int maxCh)
    {
        fs = sr;
        L = (int) std::round (0.003 * fs);            // decision lookahead
        gap = (int) std::round (0.0006 * fs);         // skip the click's own ringing
        maxW = (int) std::round (0.0015 * fs);        // max half-width (3 ms total)
        fadeLen = std::max (4, (int) std::round (0.0004 * fs));
        D = L + maxW + fadeLen + 4;
        blockLen = std::max (16, (int) std::round (0.0025 * fs));
        R = nextPow2 (2 * D + 64);
        mask = R - 1;
        ch.assign ((size_t) maxCh, {});
        for (auto& c : ch)
        {
            c.low.assign ((size_t) R, 0.0f); c.high.assign ((size_t) R, 0.0f);
            c.a.assign ((size_t) R, 0.0f);  c.g.assign ((size_t) R, 1.0f);
            c.blocks.fill (0.0f);
        }
        head = 0;
    }
    int getLatency() const { return D; }

    void process (float* const* data, int nch, int n, bool on, float sens01, float widthMs, float focusHz, float depth01)
    {
        if (focusHz != lastFocus)
        {
            for (auto& c : ch) c.lp.setLowpass (fs, focusHz, 0.70710678);
            lastFocus = focusHz;
        }
        const float thr = 4.0f * std::pow (0.4f, sens01);   // spike must exceed neighbourhood peak by this factor
        const int w = std::clamp ((int) std::round (widthMs * 0.0005 * fs), 1, maxW);

        for (int i = 0; i < n; ++i)
        {
            const int64_t h = head + i;
            const int hi = (int) (h & mask);
            for (int c = 0; c < nch; ++c)
            {
                auto& s = ch[(size_t) c];
                const float x = data[c][i];
                const float lo = s.lp.process (x);
                const float hp = x - lo;
                s.low[(size_t) hi] = lo; s.high[(size_t) hi] = hp;
                const float d2 = std::abs (hp - 2.0f * s.h1 + s.h2);
                s.h2 = s.h1; s.h1 = hp;
                s.a[(size_t) hi] = d2;

                const int64_t m = h - L;          // decision point
                if (m >= 0)
                {
                    const float am = s.a[(size_t) (m & mask)];
                    float past = s.cur;
                    for (float b : s.blocks) past = std::max (past, b);
                    if (on && s.cooldown <= 0 && am > 2.0e-3f && am > thr * past)
                    {
                        float fut = 0.0f;
                        for (int64_t k = m + gap; k <= h; ++k) fut = std::max (fut, s.a[(size_t) (k & mask)]);
                        if (am > thr * fut)
                        {
                            markRegion (s, m, w, depth01);
                            if (c == 0 || nch == 1) clicks.fetch_add (1);
                            s.cooldown = 2 * w;
                        }
                    }
                    if (s.cooldown > 0) --s.cooldown;
                    else s.cur = std::max (s.cur, am);   // clicks never enter the reference
                    if (++s.blockPos >= blockLen)
                    {
                        s.blocks[(size_t) s.blockIdx] = s.cur;
                        s.blockIdx = (s.blockIdx + 1) % (int) s.blocks.size();
                        s.cur = 0.0f; s.blockPos = 0;
                    }
                }

                const int64_t t = h - D;
                float out = 0.0f;
                if (t >= 0)
                {
                    const int ti = (int) (t & mask);
                    out = s.low[(size_t) ti] + s.high[(size_t) ti] * s.g[(size_t) ti];
                    s.minG = std::min (s.minG, s.g[(size_t) ti]);
                    s.g[(size_t) ti] = 1.0f;
                }
                data[c][i] = out;
            }
        }
        head += n;
        float mg = 1.0f;
        for (int c = 0; c < nch; ++c) { mg = std::min (mg, ch[(size_t) c].minG); ch[(size_t) c].minG = 1.0f; }
        activity.store (-gainToDb (mg));
    }

    std::atomic<int> clicks { 0 };
    std::atomic<float> activity { 0.0f };

private:
    struct Chan
    {
        Biquad lp;
        std::vector<float> low, high, a, g;
        std::array<float, 8> blocks {};      // 8 x 2.5 ms block maxima = 20 ms of history
        float cur = 0, h1 = 0, h2 = 0, minG = 1.0f;
        int blockPos = 0, blockIdx = 0, cooldown = 0;
    };

    void markRegion (Chan& s, int64_t m, int w, float depth)
    {
        for (int k = -w - fadeLen; k <= w + fadeLen; ++k)
        {
            float win = 1.0f;
            const int ak = std::abs (k);
            if (ak > w) win = 0.5f + 0.5f * std::cos ((float) kPi * (float) (ak - w) / (float) fadeLen);
            const float gv = 1.0f - depth * win;
            auto& slot = s.g[(size_t) ((m + k) & mask)];
            slot = std::min (slot, gv);
        }
    }

    double fs = 48000;
    int L = 144, gap = 29, maxW = 72, fadeLen = 19, D = 240, blockLen = 120, R = 512, mask = 511;
    int64_t head = 0;
    float lastFocus = -1;
    std::vector<Chan> ch;
};

//==============================================================================
// Plosive / pop remover. Detector: LR4 low band vs. LR4 high band of the input.
// Audio path: lookahead delay, then two cascaded low shelves whose gain is ducked when
// a pop is detected. At 0 dB the shelves are an exact identity, so it is fully
// transparent while idle.
class DePlosive
{
public:
    void prepare (double sr, int maxCh)
    {
        fs = sr;
        D = (int) std::round (0.003 * fs);
        R = nextPow2 (D + 8); mask = R - 1;
        ch.assign ((size_t) maxCh, {});
        for (auto& c : ch) c.x.assign ((size_t) R, 0.0f);
        head = 0; g = 1.0f; envL = envR = 0.0f; hold = 0; lastF = -1; lastDb = 1.0e9f;
    }
    int getLatency() const { return D; }

    void process (float* const* data, int nch, int n, bool on, float sens01, float freq, float depthDb, float releaseMs)
    {
        if (freq != lastF)
        {
            for (auto& c : ch)
            {
                c.a.setLowpass (fs, freq, 0.70710678); c.b.setLowpass (fs, freq, 0.70710678);
                c.ha.setHighpass (fs, freq, 0.70710678); c.hb.setHighpass (fs, freq, 0.70710678);
            }
            lastF = freq; lastDb = 1.0e9f;
        }
        const float rThr = 2.5f * std::pow (0.1f, sens01);   // sens 50 -> 0.79
        float maxLow = 0, maxHigh = 0;
        const float envA = msCoef (0.3f, fs), envRel = msCoef (60.0f, fs);
        const float att = msCoef (0.6f, fs), rel = msCoef (releaseMs, fs);
        float minG = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            const int64_t h = head + i;
            const int hi = (int) (h & mask);
            float lMax = 0, rMax = 0;
            for (int c = 0; c < nch; ++c)
            {
                auto& s = ch[(size_t) c];
                const float x = data[c][i];
                s.x[(size_t) hi] = x;
                lMax = std::max (lMax, std::abs (s.b.process (s.a.process (x))));
                rMax = std::max (rMax, std::abs (s.hb.process (s.ha.process (x))));
            }
            envL = lMax > envL ? envA * envL + (1 - envA) * lMax : envRel * envL + (1 - envRel) * lMax;
            envR = rMax > envR ? envA * envR + (1 - envA) * rMax : envRel * envR + (1 - envRel) * rMax;
            maxLow = std::max (maxLow, envL); maxHigh = std::max (maxHigh, envR);

            float target = 1.0f;
            if (on && envL > 0.003f)
            {
                const float r = envL / (envR + 1.0e-6f);
                const float amt = std::clamp (std::log (r / rThr) / std::log (2.0f), 0.0f, 1.0f);
                target = dbToGain (-depthDb * amt);
            }
            if (target < g - 1.0e-4f) { g = target + (g - target) * att; hold = 2 * D; }
            else if (hold > 0) --hold;
            else g = target + (g - target) * rel;
            if (g > 0.9999f) g = 1.0f;
            minG = std::min (minG, g);

            if ((i & 7) == 0)
            {
                const float db = gainToDb (g);
                if (std::abs (db - lastDb) > 0.01f)
                {
                    shelf = makeSvf (LowShelf, fs, lastF * 1.5f, 0.5 * db, 0.70710678);
                    lastDb = db;
                }
            }
            const int64_t t = h - D;
            for (int c = 0; c < nch; ++c)
            {
                auto& s = ch[(size_t) c];
                const float xd = t >= 0 ? s.x[(size_t) (t & mask)] : 0.0f;
                data[c][i] = s.s2.tick (shelf, s.s1.tick (shelf, xd));
            }
        }
        head += n;
        reduction.store (-gainToDb (minG));
        lowDb.store (gainToDb (maxLow)); highDb.store (gainToDb (maxHigh));
        thresholdRatioDb.store (gainToDb (rThr));
    }

    std::atomic<float> reduction { 0.0f }, lowDb { -100.0f }, highDb { -100.0f }, thresholdRatioDb { 0.0f };

private:
    struct Chan { Biquad a, b, ha, hb; SvfState s1, s2; std::vector<float> x; };
    double fs = 48000;
    int D = 144, R = 256, mask = 255; int64_t head = 0;
    float g = 1, envL = 0, envR = 0, lastF = -1, lastDb = 1.0e9f; int hold = 0;
    SvfCoefs shelf = makeSvf (LowShelf, 48000.0, 140.0, 0.0, 0.70710678);
    std::vector<Chan> ch;
};

//==============================================================================
// De-esser: split-band (dynamic cut of everything above the frequency) or wideband.
class DeEsser
{
public:
    void prepare (double sr, int maxCh)
    {
        fs = sr; ch.assign ((size_t) maxCh, {}); band.assign ((size_t) maxCh, 0.0f); env = 0; lastF = -1;
    }
    void process (float* const* data, int nch, int n, bool on, float freq, float threshDb, float rangeDb,
                  float releaseMs, bool wide, bool listen)
    {
        if (! on) { reduction.store (0.0f); sideDb.store (-100.0f); return; }
        if (freq != lastF)
        {
            for (auto& c : ch) { c.a.setHighpass (fs, freq, 0.54119610); c.b.setHighpass (fs, freq, 1.30656296); }
            lastF = freq; lastRed = 1.0e9f;
        }
        const float att = msCoef (0.3f, fs), rel = msCoef (releaseMs, fs);
        float maxRed = 0, maxEnv = 0;
        for (int i = 0; i < n; ++i)
        {
            float lvl = 0;
            for (int c = 0; c < nch; ++c)
            {
                auto& s = ch[(size_t) c];
                band[(size_t) c] = s.b.process (s.a.process (data[c][i]));
                lvl = std::max (lvl, std::abs (band[(size_t) c]));
            }
            env = lvl > env ? att * env + (1 - att) * lvl : rel * env + (1 - rel) * lvl;
            maxEnv = std::max (maxEnv, env);
            const float over = gainToDb (env) - threshDb;
            // soft-knee 6:1 above threshold, capped by range
            float red = 0;
            if (over > -3.0f)
            {
                const float o = over < 3.0f ? (over + 3.0f) * (over + 3.0f) / 12.0f : over;
                red = std::min (o * (1.0f - 1.0f / 6.0f), rangeDb);
            }
            maxRed = std::max (maxRed, red);
            const float g = dbToGain (-red);
            if (! wide && (i & 7) == 0 && std::abs (red - lastRed) > 0.01f)
            {
                shelf = makeSvf (HighShelf, fs, freq * 0.85, -0.5 * red, 0.70710678);
                lastRed = red;
            }
            for (int c = 0; c < nch; ++c)
            {
                const float x = data[c][i], b = band[(size_t) c];
                if (listen)     data[c][i] = b;
                else if (wide)  data[c][i] = x * g;
                else            data[c][i] = ch[(size_t) c].s2.tick (shelf, ch[(size_t) c].s1.tick (shelf, x));
            }
        }
        reduction.store (maxRed);
        sideDb.store (gainToDb (maxEnv));
    }
    std::atomic<float> reduction { 0.0f }, sideDb { -100.0f };

private:
    struct Chan { Biquad a, b; SvfState s1, s2; };
    double fs = 48000; float env = 0, lastF = -1, lastRed = 1.0e9f;
    SvfCoefs shelf = makeSvf (HighShelf, 48000.0, 5000.0, 0.0, 0.70710678);
    std::vector<Chan> ch; std::vector<float> band;
};

//==============================================================================
// Stage 1: fast FET-style compressor (1176 flavour) — feed-forward, peak detector,
// fast attack, user attack/release, ratio, "all buttons" mode, sidechain HPF, parallel mix.
class FetCompressor
{
public:
    struct Settings
    {
        bool on; float thresh, ratio, knee, attackMs, releaseMs, makeup; bool autoMakeup, allButtons; float mix01, scHpf;
    };
    void prepare (double sr, int maxCh)
    {
        fs = sr; sc.assign ((size_t) maxCh, {}); y1 = yl = 0; lastHpf = -1;
    }
    void process (float* const* data, int nch, int n, const Settings& s)
    {
        if (! s.on) { reduction.store (0.0f); sideDb.store (-100.0f); return; }
        if (s.scHpf != lastHpf) { for (auto& f : sc) f.setHighpass (fs, s.scHpf, 0.70710678); lastHpf = s.scHpf; }
        const float att = msCoef (s.attackMs, fs);
        float ratio = s.ratio, knee = s.knee, extra = 0.0f;
        if (s.allButtons) { ratio = 20.0f; knee = std::max (knee, 3.0f); extra = 1.0f; }
        float makeup = s.makeup;
        if (s.autoMakeup) makeup += (-s.thresh) * (1.0f - 1.0f / ratio) * 0.35f;
        float maxRed = 0, maxXg = -120.0f;
        for (int i = 0; i < n; ++i)
        {
            float lvl = 0;
            for (int c = 0; c < nch; ++c) lvl = std::max (lvl, std::abs (sc[(size_t) c].process (data[c][i])));
            const float xg = gainToDb (lvl);
            maxXg = std::max (maxXg, xg);
            const float d = xg - s.thresh;
            float yg;
            if (2 * d < -knee)                       yg = xg;
            else if (knee > 0 && 2 * std::abs (d) <= knee)
                yg = xg + (1.0f / ratio - 1.0f) * (d + knee / 2) * (d + knee / 2) / (2 * knee);
            else                                    yg = s.thresh + d / ratio;
            float xl = xg - yg;
            if (extra > 0 && d > 0) xl += 0.15f * d;          // all-buttons over-compression
            // program-dependent FET release: recovers faster from small amounts of GR
            const float relMs = s.releaseMs * (0.55f + 0.45f * std::min (1.0f, y1 / 12.0f));
            const float rel = msCoef (relMs, fs);
            y1 = std::max (xl, rel * y1 + (1 - rel) * xl);
            yl = att * yl + (1 - att) * y1;
            maxRed = std::max (maxRed, yl);
            const float g = dbToGain (-yl + makeup);
            const float dryG = (1.0f - s.mix01) * dbToGain (s.autoMakeup ? 0.0f : 0.0f);
            for (int c = 0; c < nch; ++c) data[c][i] = data[c][i] * (s.mix01 * g + dryG);
        }
        reduction.store (maxRed);
        sideDb.store (maxXg);
    }
    std::atomic<float> reduction { 0.0f }, sideDb { -100.0f };

private:
    double fs = 48000; std::vector<Biquad> sc; float y1 = 0, yl = 0, lastHpf = -1;
};

//==============================================================================
// Stage 2: LA-2A style opto leveller. Behaviour modelled on the T4 optical cell:
//  • fixed ~10 ms attack
//  • two-stage release: ~60 ms for the first half of recovery, then a slow tail of
//    0.5–5 s that gets longer the longer/harder the cell has been driven (memory)
//  • soft, wide knee; ~3:1 in Compress, ~10:1 in Limit
//  • Peak Reduction knob drives the cell (threshold), Gain knob is output make-up
//  • gentle HF emphasis in the sidechain (the R37 "emphasis" trim)
class OptoCompressor
{
public:
    struct Settings { bool on; float peak01, gainDb; bool limit; float emphasis01, mix01; };
    void prepare (double sr, int maxCh)
    {
        fs = sr; sc.assign ((size_t) maxCh, {}); fast = slow = charge = env = 0; lastEmph = -1;
    }
    void process (float* const* data, int nch, int n, const Settings& s)
    {
        if (! s.on) { reduction.store (0.0f); sideDb.store (-100.0f); return; }
        if (s.emphasis01 != lastEmph)
        {
            for (auto& f : sc) f.setHighShelf (fs, 3000.0, 10.0 * s.emphasis01);
            lastEmph = s.emphasis01;
        }
        const float thresh = -6.0f - 44.0f * s.peak01;          // Peak Reduction 0..100 → -6..-50 dB
        const float ratio = s.limit ? 10.0f : 3.0f;
        const float knee = s.limit ? 6.0f : 14.0f;
        const float att = msCoef (10.0f, fs);
        const float relFast = msCoef (60.0f, fs);
        const float detRel = msCoef (8.0f, fs);
        const float chargeUp = msCoef (2500.0f, fs), chargeDown = msCoef (8000.0f, fs);
        float maxRed = 0, maxXg = -120.0f;
        for (int i = 0; i < n; ++i)
        {
            float lvl = 0;
            for (int c = 0; c < nch; ++c) lvl = std::max (lvl, std::abs (sc[(size_t) c].process (data[c][i])));
            // RMS-ish light-cell drive
            const float l2 = lvl * lvl;
            env = l2 > env ? l2 : detRel * env + (1 - detRel) * l2;
            const float xg = 10.0f * std::log10 (std::max (env * 2.0f, 1.0e-12f));
            maxXg = std::max (maxXg, xg);
            const float d = xg - thresh;
            float target;
            if (2 * d < -knee)                 target = 0;
            else if (2 * std::abs (d) <= knee) target = (1.0f - 1.0f / ratio) * (d + knee / 2) * (d + knee / 2) / (2 * knee);
            else                               target = d * (1.0f - 1.0f / ratio);

            // cell memory: builds while compressing, drains slowly
            if (target > 1.0f) charge = chargeUp * charge + (1 - chargeUp) * std::min (1.0f, target / 10.0f);
            else               charge = chargeDown * charge;
            const float slowMs = 500.0f + 4500.0f * charge;
            const float relSlow = msCoef (slowMs, fs);

            fast = target > fast ? att * fast + (1 - att) * target : relFast * fast + (1 - relFast) * target;
            slow = target > slow ? att * slow + (1 - att) * target : relSlow * slow + (1 - relSlow) * target;
            const float gr = 0.55f * fast + 0.45f * slow;
            maxRed = std::max (maxRed, gr);
            const float g = dbToGain (-gr + s.gainDb);
            const float dg = (1.0f - s.mix01) * dbToGain (s.gainDb);
            for (int c = 0; c < nch; ++c) data[c][i] *= (s.mix01 * g + dg);
        }
        reduction.store (maxRed);
        sideDb.store (maxXg);
        threshDb.store (thresh);
    }
    std::atomic<float> reduction { 0.0f }, sideDb { -100.0f }, threshDb { -30.0f };

private:
    double fs = 48000; std::vector<Biquad> sc;
    float fast = 0, slow = 0, charge = 0, env = 0, lastEmph = -1;
};

} // namespace vr
