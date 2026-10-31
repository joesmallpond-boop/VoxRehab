#pragma once
// STFT repair engine: de-noise, background remover, voice isolator, breath control
// and de-reverb all share one analysis/resynthesis pass (sqrt-Hann, 75% overlap),
// each producing a per-bin gain mask. With every mask at unity the pass reconstructs
// the input exactly (to float precision).
#include "DspCore.h"

namespace vr
{
class SpectralRepair
{
public:
    struct Settings
    {
        bool dnOn = false; float dnAmount = 0.5f, dnReductionDb = 12, dnTilt = 0; bool useLearned = false, learning = false;
        bool bgOn = false; float bgThresh = -55, bgDepthDb = 12, bgReleaseMs = 200, bgRangeDb = 35, bgAmount = 0.5f;
        bool viOn = false; float viAmount = 0.5f, viLow = 90, viHigh = 12000, viFocus = 0.5f;
        bool brOn = false; float brThreshDb = 18, brReduceDb = 10;
        bool dvOn = false; float dvAmount = 0.5f, dvDecay = 0.8f, dvEarlyMs = 40, dvFloorDb = 15;
        float smooth = 0.5f;
    };

    void prepare (double sr, int maxCh)
    {
        fs = sr;
        order = fs <= 50000 ? 11 : (fs <= 100000 ? 12 : 13);
        N = 1 << order; hop = N / 4; bins = N / 2 + 1; mask = N - 1;
        hopSec = (float) hop / (float) fs;
        binHz = (float) fs / (float) N;
        fft = std::make_unique<juce::dsp::FFT> (order);
        win.resize ((size_t) N);
        winSq = 0;
        for (int i = 0; i < N; ++i)
        {
            win[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));
            winSq += win[(size_t) i] * win[(size_t) i];
        }
        buf.assign ((size_t) (2 * N), 0.0f);
        // calibrate inverse-FFT scaling (differs between FFT back-ends)
        std::fill (buf.begin(), buf.end(), 0.0f);
        buf[0] = 1.0f;
        fft->performRealOnlyForwardTransform (buf.data(), true);
        fft->performRealOnlyInverseTransform (buf.data());
        olaScale = 0.5f / (std::abs (buf[0]) > 1.0e-9f ? buf[0] : 1.0f);   // Hann at 75% overlap sums to 2

        P.assign ((size_t) bins, 0); G.assign ((size_t) bins, 1); Gtmp.assign ((size_t) bins, 1);
        tilt.assign ((size_t) bins, 1); lastTilt = 1.0e9f;
        ch.assign ((size_t) maxCh, {});
        for (auto& c : ch)
        {
            c.in.assign ((size_t) N, 0); c.out.assign ((size_t) N, 0);
            for (auto* v : { &c.Ps, &c.Nad, &c.Nest, &c.Pprev }) v->assign ((size_t) bins, 0);
            for (auto* v : { &c.Gprev, &c.Gs }) v->assign ((size_t) bins, 1);
            c.hist.assign ((size_t) kHist, std::vector<float> ((size_t) bins, 0));
            c.curMin.assign ((size_t) bins, 1.0e30f);
            for (auto& sm : c.subMin) sm.assign ((size_t) bins, 1.0e30f);
            c.subCount = 0; c.subIdx = 0; c.frames = 0;
            c.histPos = 0; c.first = true; c.gate = 1; c.breath = 1; c.peakLvl = -100;
        }
        learnAcc.assign ((size_t) bins, 0); learnFrames = 0; wasLearning = false;
        if ((int) learned.size() != bins) { learned.assign ((size_t) bins, 0); learnedValid = false; }
        pos = 0; hopCount = 0;
        subLen = std::max (2, (int) std::round (0.19 / hopSec));

        for (int pt = 0; pt < kPts; ++pt)
        {
            const double f0 = 20.0 * std::pow (1000.0, pt / (double) kPts), f1 = 20.0 * std::pow (1000.0, (pt + 1) / (double) kPts);
            int lo = (int) std::ceil (f0 / binHz), hi = (int) std::floor (f1 / binHz);
            if (hi < lo) lo = hi = (int) std::lround (std::sqrt (f0 * f1) / binHz);
            ptLo[(size_t) pt] = std::clamp (lo, 1, bins - 1); ptHi[(size_t) pt] = std::clamp (hi, 1, bins - 1);
        }
        for (auto& v : mg) v.assign ((size_t) bins, 1.0f);
        for (auto& m : maskDb) for (auto& v : m) v.store (0.0f);
        cands.clear();
        for (int i = 0; i < 240; ++i) cands.push_back (70.0f * std::pow (900.0f / 70.0f, i / 239.0f));
    }

    int getLatency() const { return N; }
    int getBins() const { return bins; }

    void process (float* const* data, int nch, int n, const Settings& s)
    {
        if (s.learning && ! wasLearning) { std::fill (learnAcc.begin(), learnAcc.end(), 0.0f); learnFrames = 0; }
        if (! s.learning && wasLearning && learnFrames > 8)
        {
            for (int k = 0; k < bins; ++k) learned[(size_t) k] = learnAcc[(size_t) k] / (float) learnFrames;
            learnedValid = true;
            profileVersion.fetch_add (1);
        }
        wasLearning = s.learning;
        if (s.dnTilt != lastTilt)
        {
            for (int k = 0; k < bins; ++k)
            {
                const float f = std::max (binHz * (float) k, 20.0f);
                tilt[(size_t) k] = std::pow (10.0f, s.dnTilt * std::log2 (f / 1000.0f) / 10.0f);
            }
            lastTilt = s.dnTilt;
        }

        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < nch; ++c)
            {
                auto& cs = ch[(size_t) c];
                cs.in[(size_t) pos] = data[c][i];
                data[c][i] = cs.out[(size_t) pos];
                cs.out[(size_t) pos] = 0;
            }
            pos = (pos + 1) & mask;
            if (++hopCount == hop)
            {
                hopCount = 0;
                for (int c = 0; c < nch; ++c) processFrame (ch[(size_t) c], s, c == 0);
                if (s.learning) ++learnFrames;
            }
        }
    }

    // learned noise profile (persisted by the processor)
    std::vector<float> learned;
    bool learnedValid = false;
    std::atomic<int> profileVersion { 0 };

    // per-frequency reduction for the monitor (gain dB at kPts log-spaced points, 20 Hz..20 kHz)
    static constexpr int kPts = 96;
    enum MaskId { MNoise = 0, MBackground, MIsolator, MBreath, MVerb, MTotal, kMasks };
    std::array<std::array<std::atomic<float>, kPts>, kMasks> maskDb {};

    // meters (dB of reduction / misc)
    std::atomic<float> mDn { 0 }, mBg { 0 }, mVi { 0 }, mBr { 0 }, mDv { 0 }, mConf { 0 }, mPitch { 0 };

private:
    static constexpr int kHist = 64, kSub = 8;
    struct Chan
    {
        std::vector<float> in, out, Ps, Nad, Nest, Pprev, Gprev, Gs;
        std::vector<std::vector<float>> hist;
        std::vector<float> curMin;
        std::array<std::vector<float>, 8> subMin;
        int histPos = 0, subCount = 0, subIdx = 0, frames = 0; bool first = true;
        float gate = 1, breath = 1, peakLvl = -100;
    };

    void processFrame (Chan& c, const Settings& s, bool meter)
    {
        float e = 0;
        for (int i = 0; i < N; ++i)
        {
            const float v = c.in[(size_t) ((pos + i) & mask)] * win[(size_t) i];
            buf[(size_t) i] = v; e += v * v;
        }
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        const float lvl = 10.0f * std::log10 (e / winSq + 1.0e-20f);
        fft->performRealOnlyForwardTransform (buf.data(), true);
        for (int k = 0; k < bins; ++k)
        {
            const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
            P[(size_t) k] = re * re + im * im;
        }

        // ---- adaptive noise estimate: windowed minimum (Martin) gates a recursive mean (MCRA).
        // The minimum is taken over ~1.5 s of sub-windows, so start-up silence and random
        // dips age out; the noise mean only updates in bins where no voice is present.
        const bool subDone = (++c.subCount >= subLen);
        for (int k = 0; k < bins; ++k)
        {
            float& ps = c.Ps[(size_t) k]; float& ne = c.Nest[(size_t) k]; float& cm = c.curMin[(size_t) k];
            const float pk = P[(size_t) k];
            if (c.frames == 0) { ps = pk; ne = pk; cm = pk; }
            ps = 0.8f * ps + 0.2f * pk;
            cm = std::min (cm, ps);
            float smin = cm;
            for (int u = 0; u < kSub; ++u) smin = std::min (smin, c.subMin[(size_t) u][(size_t) k]);
            if (subDone) { c.subMin[(size_t) c.subIdx][(size_t) k] = cm; cm = ps; }
            c.Nad[(size_t) k] = smin;
            const bool speech = ps > 4.5f * smin;
            if (! speech)            ne = 0.85f * ne + 0.15f * pk;
            if (c.frames < 40)       ne = std::max (ne, 0.85f * ne + 0.15f * pk);   // fast settle at start
            if (s.learning) learnAcc[(size_t) k] += pk / (float) ch.size();
        }
        if (subDone) { c.subCount = 0; c.subIdx = (c.subIdx + 1) % kSub; }
        ++c.frames;

        const int kLo = std::max (1, (int) (100.0f / binHz)), kHi = std::min (bins - 1, (int) (8000.0f / binHz));
        double sumDn = 0, sumBg = 0, sumVi = 0, sumDv = 0;

        // ---- pitch / harmonicity (for isolator + breath)
        float f0 = 0, conf = 0;
        if (s.viOn || s.brOn) analysePitch (f0, conf, lvl);

        // ---- frame-level gates
        const float relC = std::exp (-hopSec / std::max (0.02f, s.bgReleaseMs * 0.001f));
        const float gateTarget = (s.bgOn && lvl < s.bgThresh) ? dbToGain (-s.bgDepthDb) : 1.0f;
        c.gate = gateTarget >= c.gate ? gateTarget : gateTarget + (c.gate - gateTarget) * relC;

        c.peakLvl = std::max (lvl, c.peakLvl - 8.0f * hopSec);
        float centroidNum = 0, centroidDen = 1.0e-20f;
        for (int k = kLo; k < kHi; ++k) { centroidNum += P[(size_t) k] * k; centroidDen += P[(size_t) k]; }
        const float centroid = centroidNum / centroidDen * binHz;
        const bool isBreath = s.brOn && conf < 0.35f && lvl < c.peakLvl - s.brThreshDb && lvl > -80.0f && centroid < 5000.0f;
        const float brT = isBreath ? dbToGain (-s.brReduceDb) : 1.0f;
        c.breath = brT < c.breath ? brT + (c.breath - brT) * 0.45f : brT + (c.breath - brT) * 0.6f;

        float pMax = 1.0e-20f;
        for (int k = kLo; k < kHi; ++k) pMax = std::max (pMax, P[(size_t) k]);
        const float bgThrP = pMax * std::pow (10.0f, -s.bgRangeDb / 10.0f);

        // ---- de-verb setup
        const int nd = std::clamp ((int) std::round (s.dvEarlyMs * 0.001f / hopSec), 1, kHist - 1);
        const float delta = 3.0f * std::log (10.0f) / std::max (0.05f, s.dvDecay);
        const float decay = std::exp (-2.0f * delta * nd * hopSec);
        const auto& past = c.hist[(size_t) ((c.histPos - nd + kHist) % kHist)];
        const float dvFloor = dbToGain (-s.dvFloorDb);
        const float dvAmt = 2.0f * s.dvAmount;

        const float alpha = 0.92f + 0.07f * s.smooth;
        const float over = 0.5f + 3.5f * s.dnAmount;
        const float dnFloor = dbToGain (-s.dnReductionDb);
        const bool haveLearned = s.useLearned && learnedValid && (int) learned.size() == bins;
        const float tol = std::max (1.0f, f0 * (0.30f - 0.22f * s.viFocus));

        for (int k = 0; k < bins; ++k)
        {
            const float p = P[(size_t) k], f = binHz * (float) k;
            float g = 1.0f;

            // de-noise: decision-directed Wiener
            float gdn = 1.0f;
            {
                const float noise = haveLearned ? learned[(size_t) k] : c.Nest[(size_t) k] * 1.2f;
                const float nk = noise * over * tilt[(size_t) k] + 1.0e-20f;
                const float post = p / nk;
                const float prio = alpha * c.Gprev[(size_t) k] * c.Gprev[(size_t) k] * c.Pprev[(size_t) k] / nk
                                 + (1.0f - alpha) * std::max (post - 1.0f, 0.0f);
                gdn = std::max (prio / (1.0f + prio), dnFloor);
                c.Gprev[(size_t) k] = gdn; c.Pprev[(size_t) k] = p;
                if (! s.dnOn) gdn = 1.0f;
            }
            g *= gdn;

            // background remover: frame gate + spectral expander under the voice
            float gbg = c.gate;
            if (s.bgOn && p < bgThrP)
            {
                const float gs = std::max (std::sqrt (p / bgThrP), 0.01f);
                gbg *= 1.0f - s.bgAmount * (1.0f - gs);
            }
            g *= gbg;

            // voice isolator: band-limit + harmonic comb in voiced frames
            float gvi = 1.0f;
            if (s.viOn)
            {
                float band = 1.0f;
                if (f < s.viLow)  band = std::max (0.01f, (f / s.viLow) * (f / s.viLow));
                if (f > s.viHigh) band = std::max (0.01f, (s.viHigh / f) * (s.viHigh / f));
                gvi = 1.0f - s.viAmount * (1.0f - band);
                if (conf > 0 && f < 6000.0f)
                {
                    const float h = std::max (1.0f, std::round (f / f0));
                    const float d = (f - h * f0) / tol;
                    const float w = std::exp (-0.5f * d * d);
                    const float mk = w + (1.0f - w) * 0.1f;
                    const float fade = f < 3000.0f ? 1.0f : (6000.0f - f) / 3000.0f;
                    gvi *= 1.0f - s.viAmount * conf * fade * (1.0f - mk);
                }
            }
            g *= gvi;

            g *= c.breath;

            // de-verb: late reverberation estimate from the exponential decay model
            float gdv = 1.0f;
            if (s.dvOn)
            {
                const float late = decay * past[(size_t) k];
                gdv = std::max (1.0f - dvAmt * late / (p + 1.0e-20f), dvFloor);
            }
            g *= gdv;

            G[(size_t) k] = std::max (g, 0.001f);
            if (meter && k >= kLo && k < kHi) { sumDn += gdn; sumBg += gbg; sumVi += gvi; sumDv += gdv; }
            if (meter)
            {
                mg[MNoise][(size_t) k] = gdn; mg[MBackground][(size_t) k] = gbg; mg[MIsolator][(size_t) k] = gvi;
                mg[MBreath][(size_t) k] = c.breath; mg[MVerb][(size_t) k] = gdv;
            }
        }
        c.hist[(size_t) c.histPos] = P;
        c.histPos = (c.histPos + 1) % kHist;

        // ---- artifact control: frequency + time smoothing of the combined mask
        const float fc = 0.5f * s.smooth, tr = 0.5f * s.smooth;
        Gtmp = G;
        for (int k = 1; k < bins - 1; ++k)
            G[(size_t) k] = (1 - fc) * Gtmp[(size_t) k] + fc * 0.5f * (Gtmp[(size_t) (k - 1)] + Gtmp[(size_t) (k + 1)]);
        for (int k = 0; k < bins; ++k)
        {
            float& gs = c.Gs[(size_t) k];
            gs = tr * gs + (1 - tr) * G[(size_t) k];
            buf[(size_t) (2 * k)] *= gs;
            buf[(size_t) (2 * k + 1)] *= gs;
        }
        fft->performRealOnlyInverseTransform (buf.data());
        for (int i = 0; i < N; ++i)
            c.out[(size_t) ((pos + i) & mask)] += buf[(size_t) i] * win[(size_t) i] * olaScale;

        if (meter)
        {
            mg[MTotal] = c.Gs;
            for (int pt = 0; pt < kPts; ++pt)
            {
                const int lo = ptLo[(size_t) pt], hi = ptHi[(size_t) pt];
                for (int m = 0; m < kMasks; ++m)
                {
                    float sum = 0;
                    for (int k = lo; k <= hi; ++k) sum += mg[(size_t) m][(size_t) k];
                    const float db = gainToDb (sum / (float) (hi - lo + 1));
                    auto& slot = maskDb[(size_t) m][(size_t) pt];
                    slot.store (0.7f * slot.load() + 0.3f * db);
                }
            }
            const double nb = std::max (1, kHi - kLo);
            mDn.store (-gainToDb ((float) (sumDn / nb)));
            mBg.store (-gainToDb ((float) (sumBg / nb)));
            mVi.store (-gainToDb ((float) (sumVi / nb)));
            mBr.store (-gainToDb (c.breath));
            mDv.store (-gainToDb ((float) (sumDv / nb)));
            mConf.store (conf); mPitch.store (f0);
        }
    }

    void analysePitch (float& f0, float& conf, float lvl)
    {
        f0 = 0; conf = 0;
        if (lvl < -60.0f) return;
        auto mag = [this] (float f) { const int k = (int) std::lround (f / binHz); return k < bins ? std::sqrt (P[(size_t) k]) : 0.0f; };
        float best = 0, bestF = 0;
        for (float cf : cands)
        {
            float sc = 0, wgt = 1;
            for (int h = 1; h <= 8; ++h)
            {
                const float fh = cf * h;
                if (fh > 5000) break;
                const int k = (int) (fh / binHz);
                if (k + 1 >= bins) break;
                sc += wgt * std::max (std::sqrt (P[(size_t) k]), std::sqrt (P[(size_t) (k + 1)]));
                wgt *= 0.85f;
            }
            if (sc > best) { best = sc; bestF = cf; }
        }
        if (bestF <= 0) return;
        // harmonicity: energy on harmonics vs. half-way between them
        float on = 0, offv = 0;
        for (int h = 1; h <= 12; ++h)
        {
            const float fh = bestF * h;
            if (fh > 4500) break;
            const float m1 = mag (fh), m2 = mag (fh + 0.5f * bestF);
            on += m1 * m1; offv += m2 * m2;
        }
        const float hr = on / (on + offv + 1.0e-20f);
        conf = std::clamp ((hr - 0.75f) / 0.17f, 0.0f, 1.0f);
        f0 = bestF;
    }

    double fs = 48000;
    int order = 11, N = 2048, hop = 512, bins = 1025, mask = 2047, pos = 0, hopCount = 0;
    float hopSec = 0.01f, binHz = 23.4f, winSq = 1, olaScale = 0.5f, lastTilt = 1.0e9f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> win, buf, P, G, Gtmp, tilt, learnAcc, cands;
    int learnFrames = 0, subLen = 18; bool wasLearning = false;
    std::vector<Chan> ch;
    std::array<int, kPts> ptLo {}, ptHi {};
    std::array<std::vector<float>, kMasks> mg;
};
} // namespace vr
