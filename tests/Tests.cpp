// Offline measurements for Vox Rehab's DSP. Prints numbers; non-zero exit on hard failures.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <random>
#include <chrono>
#include <cstdio>

static constexpr double FS = 48000.0;
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { ++failures; std::printf ("  ** FAIL: %s\n", msg); } } while (0)

struct Rig
{
    VoxRehabProcessor p;
    int bs = 256;
    Rig()
    {
        p.setPlayConfigDetails (2, 2, FS, bs);
        p.prepareToPlay (FS, bs);
        allOff();
    }
    void set (const juce::String& id, float v)
    {
        auto* prm = p.apvts.getParameter (id);
        if (prm == nullptr) { std::printf ("unknown param %s\n", id.toRawUTF8()); std::exit (2); }
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }
    void allOff()
    {
        for (auto* id : { ids::hpf_on, ids::hum_on, ids::dc_on, ids::dp_on, ids::dn_on, ids::bg_on, ids::vi_on, ids::br_on,
                          ids::dv_on, ids::de_on, ids::c_on, ids::op_on, ids::delta, ids::de_listen, ids::gl_on })
            set (id, 0);
        for (int b = 0; b < 8; ++b) set (eqId (b, "on"), 0);
        set (ids::mix, 100); set (ids::out_gain, 0); set (ids::in_gain, 0);
    }
    int latency() const { return p.getLatencySamples(); }
    std::vector<float> run (const std::vector<float>& in)
    {
        std::vector<float> out (in.size());
        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;
        for (size_t s = 0; s < in.size(); s += (size_t) bs)
        {
            const int n = (int) std::min ((size_t) bs, in.size() - s);
            buf.setSize (2, n, false, false, true);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) buf.setSample (c, i, in[s + (size_t) i]);
            p.processBlock (buf, midi);
            for (int i = 0; i < n; ++i) out[s + (size_t) i] = buf.getSample (0, i);
        }
        return out;
    }
};

static float rmsDb (const std::vector<float>& x, size_t a, size_t b)
{
    double s = 0; b = std::min (b, x.size());
    for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return (float) (10.0 * std::log10 (s / std::max<size_t> (1, b - a) + 1e-20));
}

// synthetic voice: vibrato glottal-ish harmonics through 3 formants, phrased
struct Voice
{
    std::vector<float> sig; std::vector<char> voiced;
};
static Voice makeVoice (double seconds, unsigned seed = 1)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> u (-1, 1);
    const size_t n = (size_t) (seconds * FS);
    Voice v; v.sig.assign (n, 0); v.voiced.assign (n, 0);
    double ph = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = i / FS;
        const double cyc = std::fmod (t, 2.2);
        double env = 0;
        if (cyc > 0.1 && cyc < 1.5) env = std::min ({ 1.0, (cyc - 0.1) / 0.03, (1.5 - cyc) / 0.03 });
        const double f0 = 140.0 * (1 + 0.025 * std::sin (2 * vr::kPi * 5.2 * t)) * (1 + 0.15 * std::sin (2 * vr::kPi * 0.31 * t));
        ph += 2 * vr::kPi * f0 / FS;
        double s = 0;
        for (int h = 1; h * f0 < 9000; ++h)
        {
            const double f = h * f0;
            auto bump = [f] (double c, double w, double a) { return a * std::exp (-0.5 * std::pow ((f - c) / w, 2)); };
            const double a = (1.0 / h) * (0.3 + bump (550, 120, 3) + bump (1600, 200, 1.6) + bump (2700, 300, 1.0));
            s += a * std::sin (h * ph);
        }
        v.sig[i] = (float) (0.06 * env * s);
        v.voiced[i] = env > 0.99 ? 1 : 0;
    }
    float pk = 0; for (auto x : v.sig) pk = std::max (pk, std::abs (x));
    for (auto& x : v.sig) x *= 0.3f / pk;   // ~ -10 dBFS peak
    return v;
}

static std::vector<float> noise (size_t n, float dbfsRms, unsigned seed)
{
    std::mt19937 rng (seed); std::normal_distribution<float> d (0, 1);
    std::vector<float> x (n); const float a = vr::dbToGain (dbfsRms);
    for (auto& s : x) s = a * d (rng);
    return x;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;



    if (const char* dir = std::getenv ("VR_SNAP"))
    {
        auto v = makeVoice (12.0);
        const size_t n = v.sig.size();
        auto nz = noise (n, -42, 9), hiss = noise (n, -14, 21);
        vr::Biquad bp; bp.setBandpass (FS, 7000, 1.2);
        std::vector<float> in (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double t = i / FS, c = std::fmod (t, 2.2);
            float x = v.sig[i] + nz[i];
            const float h = bp.process (hiss[i]);
            if (c > 0.7 && c < 0.85) x += h;                                    // "s"
            if (c > 0.12 && c < 0.2)                                            // pop at phrase start
                x += 0.8f * (float) (std::exp (-(c - 0.12) / 0.025) * std::sin (2 * vr::kPi * 90 * (c - 0.12)));
            const double tc = std::fmod (t, 0.83);                              // mouth clicks
            if (tc < 30.0 / FS && t > 0.3) { const int j = (int) (tc * FS); x += 0.25f * std::exp (-j / 6.0f) * ((j & 1) ? -1.0f : 1.0f); }
            in[i] = x;
        }
        Rig r;
        r.p.setCurrentProgram (0);
        r.set (ids::op_on, 1); r.set (ids::dv_on, 1); r.set (ids::br_on, 1);
        r.set (ids::gl_on, 1); r.set (ids::gl_mode, 3); r.set (ids::gl_glue, 45); r.set (ids::gl_drive, 40);
        r.set (eqId (4, "on"), 1); r.set (eqId (4, "freq"), 3000); r.set (eqId (4, "dyn"), 1); r.set (eqId (4, "gain"), 2);
        r.set (eqId (2, "on"), 1); r.set (eqId (2, "freq"), 300); r.set (eqId (2, "gain"), -3);
        std::unique_ptr<VoxRehabEditor> ed (dynamic_cast<VoxRehabEditor*> (r.p.createEditor()));
        ed->setSize (VoxRehabEditor::kW, VoxRehabEditor::kH);
        // feed audio in pieces and tick the UI like the timer would
        const int blk = 1600;
        for (size_t s0 = 0; s0 + blk < n; s0 += blk)
        {
            juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer midi;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < blk; ++i) b.setSample (c, i, in[s0 + (size_t) i]);
            r.p.processBlock (b, midi);
            ed->tickForTest();
        }
        {
            const auto& tr = r.p.monitor.tr[Monitor::Plosive];
            float mg = 0, ml = -200, mh = -200;
            for (int i = 0; i < Monitor::kLen; ++i) { mg = std::max (mg, tr.gr[(size_t) i]); ml = std::max (ml, tr.side[(size_t) i]); mh = std::max (mh, tr.side2[(size_t) i]); }
            std::printf ("plosive monitor: max GR %.1f, max low %.1f, max high %.1f, dp_on %.0f sens %.0f\n", mg, ml, mh,
                         r.p.apvts.getRawParameterValue (ids::dp_on)->load(), r.p.apvts.getRawParameterValue (ids::dp_sens)->load());
        }
        const std::pair<int, const char*> pages[] { { 1, "eq" }, { 100 + Monitor::DeEss, "deess" }, { 100 + Monitor::Plosive, "plosive" },
                                                    { 100 + Monitor::Noise, "denoise" }, { 100 + Monitor::Fet, "fet" },
                                                    { 100 + Monitor::Click, "declick" }, { 100 + Monitor::Overall, "overall" },
                                                    { 100 + Monitor::Glue, "glue" } };
        for (auto& [id, name] : pages)
        {
            ed->selectView (id);
            for (int k = 0; k < 3; ++k) ed->tickForTest();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = juce::File (dir).getChildFile (juce::String ("monitor_") + name + ".png");
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
            std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
        }
        {
            using VP = VoxRehabProcessor;
            r.p.setOrder ({ VP::StGlue, VP::StHum, VP::StClick, VP::StPlosive, VP::StSpectral, VP::StFet, VP::StOpto, VP::StEq, VP::StDeEss });
            ed->selectView (1);
            ed->tickForTest();
            juce::Desktop::getInstance().getAnimator().cancelAllAnimations (true);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = juce::File (dir).getChildFile ("reordered.png"); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat().writeImageToStream (img, os);
        }
        return 0;
    }

    if (std::getenv ("VR_FETPROBE"))
    {
        vr::FetCompressor f; f.prepare (48000, 2);
        vr::FetCompressor::Settings st { true, 0.0f, 4.0f, 3.0f, 0.8f, 120.0f, 0.0f, false, false, 1.0f, 90.0f };
        std::mt19937 rng (1); std::normal_distribution<float> d (0, 0.0316f);
        std::vector<float> a (512), b (512), ia (512); float* chp[2] = { a.data(), b.data() };
        double maxDev = 0; int devs = 0;
        for (int blk = 0; blk < 200; ++blk)
        {
            for (int i = 0; i < 512; ++i) { ia[(size_t) i] = d (rng); a[(size_t) i] = b[(size_t) i] = ia[(size_t) i]; }
            f.process (chp, 2, 512, st);
            for (int i = 0; i < 512; ++i) if (a[(size_t) i] != ia[(size_t) i]) { ++devs; maxDev = std::max (maxDev, (double) std::abs (a[(size_t) i] / ia[(size_t) i] - 1)); }
        }
        Rig r; r.set (ids::c_on, 1); r.set (ids::c_thresh, 0); r.set (ids::c_auto, 0);
        std::printf ("direct: changed %d max dev %.3g | params: mix %.9g makeup %.9g thresh %.9g auto %.9g knee %.9g\n", devs, maxDev,
                     r.p.apvts.getRawParameterValue (ids::c_mix)->load(), r.p.apvts.getRawParameterValue (ids::c_makeup)->load(),
                     r.p.apvts.getRawParameterValue (ids::c_thresh)->load(), r.p.apvts.getRawParameterValue (ids::c_auto)->load(),
                     r.p.apvts.getRawParameterValue (ids::c_knee)->load());
        return 0;
    }
    if (std::getenv ("VR_PROBE"))
    {
        for (float red : { 12.0f, 24.0f, 40.0f })
            for (float sm : { 0.0f, 50.0f })
            {
                Rig r; r.set (ids::dn_on, 1); r.set (ids::dn_amount, 40); r.set (ids::dn_reduction, red); r.set (ids::sp_smooth, sm);
                auto in = noise ((size_t) (6 * FS), -35, 3);
                auto out = r.run (in);
                std::printf ("noise only: red %2.0f smooth %2.0f -> out %.1f dB (in -35), meter %.1f\n", red, sm, rmsDb (out, (size_t) (3 * FS), in.size()), r.p.spectral.mDn.load());
            }
        return 0;
    }
    // 1. transparency + latency
    {
        std::printf ("\n[1] Transparency (all modules off) + latency\n");
        Rig r;
        auto in = noise ((size_t) (3 * FS), -12, 7);
        auto out = r.run (in);
        const int L = r.latency();
        double err = 0, sig = 0;
        for (size_t i = (size_t) L + 4096; i < in.size(); ++i) { const double d = out[i] - in[i - (size_t) L]; err += d * d; sig += (double) in[i - (size_t) L] * in[i - (size_t) L]; }
        const double nullDb = 10 * std::log10 (err / sig + 1e-30);
        std::printf ("  latency %d samples (%.1f ms); null depth %.1f dB\n", L, 1000.0 * L / FS, nullDb);
        CHECK (err == 0.0, "all modules off must be bit-exact");
        std::vector<float> imp ((size_t) (FS / 2), 0); imp[1000] = 1.0f;
        auto io = r.run (imp);
        size_t pk = 0; for (size_t i = 0; i < io.size(); ++i) if (std::abs (io[i]) > std::abs (io[pk])) pk = i;
        std::printf ("  impulse peak at %zu (expected %d)\n", pk, 1000 + L);
        CHECK ((int) pk == 1000 + L, "latency report must match measured delay");
    }


    // 1b. modules switched ON but idle must also be transparent
    {
        std::printf ("\n[1b] Modules on but idle (de-click, de-plosive, de-ess, FET above signal, EQ bands at 0 dB, spectral off)\n");
        Rig r;
        for (auto* id : { ids::dc_on, ids::dp_on, ids::de_on, ids::c_on }) r.set (id, 1);
        r.set (ids::de_thresh, 0); r.set (ids::c_thresh, 0); r.set (ids::c_auto, 0);
        for (int b = 0; b < 8; ++b) { r.set (eqId (b, "on"), 1); r.set (eqId (b, "gain"), 0); }
        r.set (eqId (0, "on"), 0); r.set (eqId (7, "on"), 0);   // cut filters are never neutral
        auto in = noise ((size_t) (3 * FS), -30, 8);
        auto out = r.run (in);
        const int L = r.latency();
        double err = 0, sig = 0;
        for (size_t i = (size_t) L + 8192; i < in.size(); ++i) { const double d = out[i] - in[i - (size_t) L]; err += d * d; sig += (double) in[i - (size_t) L] * in[i - (size_t) L]; }
        std::printf ("  null depth %.1f dB%s\n", 10 * std::log10 (err / sig + 1e-30), err == 0 ? " (bit-exact)" : "");
        CHECK (err / sig < 1e-12, "idle modules should null below -120 dB");
        for (auto* id : { ids::dc_on, ids::dp_on, ids::de_on, ids::c_on, ids::eq_on })
        {
            Rig q; q.set (id, 1); q.set (ids::de_thresh, 0); q.set (ids::c_thresh, 0); q.set (ids::c_auto, 0);
            if (id == ids::eq_on) for (int b = 1; b < 7; ++b) q.set (eqId (b, "on"), 1);
            auto o2 = q.run (in); double e2 = 0;
            for (size_t i = (size_t) L + 8192; i < in.size(); ++i) { const double d = o2[i] - in[i - (size_t) L]; e2 += d * d; }
            std::printf ("    %-6s alone: %s\n", id, e2 == 0 ? "bit-exact" : juce::String (10 * std::log10 (e2 / sig), 1).toRawUTF8());
        }
    }

    // 1c. chain reordering
    {
        std::printf ("\n[1c] Chain order\n");
        Rig r;
        auto o = VoxRehabProcessor::defaultOrder();
        std::reverse (o.begin(), o.end());
        r.p.setOrder (o);
        auto in = noise ((size_t) FS, -20, 2);
        auto out = r.run (in);
        double err = 0; const int L = r.latency();
        for (size_t i = (size_t) L; i < in.size(); ++i) err += std::abs (out[i] - in[i - (size_t) L]);
        juce::MemoryBlock mb; r.p.getStateInformation (mb);
        Rig r2; r2.p.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("  reversed order: latency %d, bit-exact when idle: %s, restored from state: %s\n", L, err == 0 ? "yes" : "no",
                     r2.p.getOrder() == o ? "yes" : "no");
        CHECK (r2.p.getOrder() == o, "order must persist");
        CHECK (! VoxRehabProcessor::isValid ({ 0, 0, 1, 2, 3, 4, 5, 6, 7 }), "invalid orders rejected");
    }

    // 1d. glue / console characters
    {
        std::printf ("\n[1d] Glue characters (1 kHz sine at -12 dBFS, drive 50%%, colour 0, glue 0)\n");
        const char* names[] { "Vintage Tape", "Tube (Siemens)", "API Console", "Neve Console" };
        for (int m = 0; m < 4; ++m)
        {
            Rig r; r.set (ids::gl_on, 1); r.set (ids::gl_mode, (float) m); r.set (ids::gl_drive, 50); r.set (ids::gl_color, 0); r.set (ids::gl_glue, 0);
            std::vector<float> in ((size_t) FS);
            for (size_t i = 0; i < in.size(); ++i) in[i] = 0.25f * (float) std::sin (2 * vr::kPi * 1000.0 * i / FS);
            auto out = r.run (in);
            auto harm = [&] (int h)
            {
                double re = 0, im = 0;
                for (size_t i = (size_t) (0.5 * FS); i < out.size(); ++i)
                { const double ph = 2 * vr::kPi * 1000.0 * h * i / FS; re += out[i] * std::cos (ph); im += out[i] * std::sin (ph); }
                return 20 * std::log10 (std::sqrt (re * re + im * im) / (0.5 * FS) * 2 + 1e-12);
            };
            const double h1 = harm (1);
            std::printf ("  %-15s fundamental %.1f dB | H2 %6.1f dB  H3 %6.1f dB  H5 %6.1f dB (rel.)\n", names[m], h1,
                         harm (2) - h1, harm (3) - h1, harm (5) - h1);
        }
        Rig r; r.set (ids::gl_on, 1); r.set (ids::gl_drive, 0); r.set (ids::gl_color, 0); r.set (ids::gl_glue, 0); r.set (ids::gl_mix, 0);
        std::vector<float> imp ((size_t) (FS / 2), 0); imp[1000] = 1.0f;
        auto io = r.run (imp);
        size_t pk = 0; for (size_t i = 0; i < io.size(); ++i) if (std::abs (io[i]) > std::abs (io[pk])) pk = i;
        std::printf ("  glue on, mix 0: impulse at %zu (expected %d), value %.6f\n", pk, 1000 + r.latency(), io[pk]);
        CHECK ((int) pk == 1000 + r.latency(), "glue latency must be compensated");
    }

    auto voice = makeVoice (20.0);
    const size_t N = voice.sig.size();

    // 2. de-noise
    {
        for (float nl : { -50.0f, -35.0f }) {
        std::printf ("\n[2] De-noise (voice + white noise @ %.0f dBFS RMS)\n", nl);
        auto nz = noise (N, nl, 3);
        std::vector<float> in (N); for (size_t i = 0; i < N; ++i) in[i] = voice.sig[i] + nz[i];
        for (int pass = 0; pass < 2; ++pass)
        {
            Rig r; r.set (ids::dn_on, 1); r.set (ids::dn_amount, 40); r.set (ids::dn_reduction, pass == 0 ? 12 : 24);
            auto out = r.run (in); const size_t L = (size_t) r.latency();
            double gapIn = 0, gapOut = 0, vs = 0, eIn = 0, eOut = 0; size_t gn = 0;
            for (size_t i = (size_t) (4 * FS); i + L < N; ++i)
            {
                const double o = out[i + L];
                if (std::fmod (i / FS, 2.2) > 1.62 && std::fmod (i / FS, 2.2) < 2.18) { gapIn += (double) in[i] * in[i]; gapOut += o * o; ++gn; }
                if (voice.voiced[i]) { vs += (double) voice.sig[i] * voice.sig[i]; eIn += (double) nz[i] * nz[i]; eOut += (o - voice.sig[i]) * (o - voice.sig[i]); }
            }
            std::printf ("  reduction %2d dB: noise in gaps %.1f dB -> %.1f dB | voiced SNR %.1f dB -> %.1f dB\n",
                         pass == 0 ? 12 : 24, 10 * std::log10 (gapIn / gn), 10 * std::log10 (gapOut / gn + 1e-30),
                         10 * std::log10 (vs / eIn), 10 * std::log10 (vs / eOut));
        }
        }
        // clean-voice damage with de-noise on (should be small)
        Rig r; r.set (ids::dn_on, 1); r.set (ids::dn_amount, 40); r.set (ids::dn_reduction, 12);
        auto out = r.run (voice.sig); const size_t L = (size_t) r.latency();
        double vs = 0, e = 0;
        for (size_t i = (size_t) (4 * FS); i + L < N; ++i) if (voice.voiced[i]) { vs += (double) voice.sig[i] * voice.sig[i]; const double d = out[i + L] - voice.sig[i]; e += d * d; }
        std::printf ("  clean voice through de-noise: residual %.1f dB below signal\n", 10 * std::log10 (vs / e));
    }

    // 3. de-click
    {
        std::printf ("\n[3] De-click (40 synthetic mouth clicks)\n");
        std::vector<float> in = voice.sig;
        std::vector<size_t> pos;
        std::mt19937 rng (11);
        for (int k = 0; k < 40; ++k)
        {
            const size_t p0 = (size_t) (FS * (0.5 + 19.0 * k / 40.0)) + (rng() % 2000);
            pos.push_back (p0);
            for (int j = 0; j < 30; ++j) in[p0 + (size_t) j] += 0.25f * std::exp (-j / 6.0f) * ((j & 1) ? -1.0f : 1.0f);
        }
        Rig r; r.set (ids::dc_on, 1);
        auto out = r.run (in); const size_t L = (size_t) r.latency();
        const int detected = r.p.deClick.clicks.load();
        double resIn = 0, resOut = 0;
        for (auto p0 : pos)
            for (size_t j = p0; j < p0 + 48; ++j) { const double a = in[j] - voice.sig[j], b = out[j + L] - voice.sig[j]; resIn += a * a; resOut += b * b; }
        std::printf ("  detections: %d | click energy %.1f dB -> %.1f dB (removed %.1f dB)\n", detected,
                     10 * std::log10 (resIn), 10 * std::log10 (resOut), 10 * std::log10 (resIn / resOut));
        Rig r2; r2.set (ids::dc_on, 1); r2.run (voice.sig);
        std::printf ("  false detections on clean voice: %d\n", r2.p.deClick.clicks.load());
        CHECK (detected >= 30, "should catch most clicks");
    }

    // 4. de-plosive
    {
        std::printf ("\n[4] De-plosive (6 low-frequency pops)\n");
        std::vector<float> in = voice.sig, pop (N, 0);
        for (int k = 0; k < 6; ++k)
        {
            const size_t p0 = (size_t) (FS * (0.12 + 2.2 * k * 1.5));
            for (int j = 0; j < (int) (0.08 * FS); ++j)
            {
                const float t = j / (float) FS;
                pop[p0 + (size_t) j] = 0.6f * std::exp (-t / 0.02f) * std::sin (2 * (float) vr::kPi * 55 * t) * std::min (1.0f, t / 0.002f);
            }
        }
        for (size_t i = 0; i < N; ++i) in[i] += pop[i];
        Rig r; r.set (ids::dp_on, 1);
        auto out = r.run (in); const size_t L = (size_t) r.latency();
        double pIn = 0, pOut = 0;
        vr::Biquad l1, l2; l1.setLowpass (FS, 100, 0.707); l2.setLowpass (FS, 100, 0.707);
        std::vector<float> lin (N), lout (N);
        for (size_t i = 0; i < N; ++i) { lin[i] = l1.process (in[i]); lout[i] = l2.process (out[i]); }
        for (size_t i = 0; i + L < N; ++i) if (pop[i] != 0) { pIn += (double) lin[i] * lin[i]; pOut += (double) lout[i + L] * lout[i + L]; }
        std::printf ("  pop energy below 100 Hz: %.1f dB -> %.1f dB  \n", 10 * std::log10 (pIn), 10 * std::log10 (pOut));
        Rig r2; r2.set (ids::dp_on, 1);
        auto o2 = r2.run (voice.sig);
        double vs = 0, e = 0;
        for (size_t i = (size_t) FS; i + L < N; ++i) { vs += (double) voice.sig[i] * voice.sig[i]; const double d = o2[i + L] - voice.sig[i]; e += d * d; }
        std::printf ("  clean voice untouched: residual %.1f dB below signal\n", 10 * std::log10 (vs / e));
    }


    // 4b. de-plosive band mode (boomy 250 Hz thumps)
    {
        std::printf ("\n[4b] De-plosive Band mode (250 Hz boom bursts, band 250 Hz x 1.5 oct)\n");
        std::vector<float> in = voice.sig, boom (N, 0);
        for (int k = 0; k < 6; ++k)
        {
            const size_t p0 = (size_t) (FS * (0.3 + 2.2 * k * 1.5));
            for (int j = 0; j < (int) (0.07 * FS); ++j)
            {
                const float t = j / (float) FS;
                boom[p0 + (size_t) j] = 0.5f * std::exp (-t / 0.02f) * std::sin (2 * (float) vr::kPi * 250 * t) * std::min (1.0f, t / 0.002f);
            }
        }
        for (size_t i = 0; i < N; ++i) in[i] += boom[i];
        Rig r; r.set (ids::dp_on, 1); r.set (ids::dp_mode, 1); r.set (ids::dp_freq, 250); r.set (ids::dp_width, 1.5f); r.set (ids::dp_sens, 60);
        auto out = r.run (in); const size_t L = (size_t) r.latency();
        vr::Biquad b1, b2; b1.setBandpass (FS, 250, 2.0); b2.setBandpass (FS, 250, 2.0);
        std::vector<float> bi (N), bo (N);
        for (size_t i = 0; i < N; ++i) { bi[i] = b1.process (in[i]); bo[i] = b2.process (out[i]); }
        double eI = 0, eO = 0;
        for (size_t i = 0; i + L < N; ++i) if (boom[i] != 0) { eI += (double) bi[i] * bi[i]; eO += (double) bo[i + L] * bo[i + L]; }
        std::printf ("  boom energy around 250 Hz: %.1f dB -> %.1f dB (cut %.1f dB), max ratio %.2f\n", 10 * std::log10 (eI), 10 * std::log10 (eO), 10 * std::log10 (eI / eO), r.p.dePlosive.maxRatio);
        Rig r2; r2.set (ids::dp_on, 1); r2.set (ids::dp_mode, 1); r2.set (ids::dp_freq, 250); r2.set (ids::dp_width, 1.5f); r2.set (ids::dp_sens, 60);
        auto o2 = r2.run (voice.sig);
        double vs = 0, e = 0;
        for (size_t i = (size_t) FS; i + L < N; ++i) { vs += (double) voice.sig[i] * voice.sig[i]; const double d = o2[i + L] - voice.sig[i]; e += d * d; }
        std::printf ("  clean voice in band mode: residual %.1f dB below signal (max ratio %.2f)\n", 10 * std::log10 (vs / (e + 1e-30)), r2.p.dePlosive.maxRatio);
    }

    // 5. de-verb (Schroeder reverb, RT60 ~0.9 s)
    {
        std::printf ("\n[5] De-verb\n");
        auto rev = voice.sig;
        {
            const int combD[4] { 1557, 1617, 1491, 1422 }; const int apD[2] { 225, 556 };
            std::vector<std::vector<float>> cb (4); for (int k = 0; k < 4; ++k) cb[(size_t) k].assign ((size_t) combD[k], 0);
            std::vector<std::vector<float>> ab (2); for (int k = 0; k < 2; ++k) ab[(size_t) k].assign ((size_t) apD[k], 0);
            int ci[4] {}, ai[2] {};
            for (size_t i = 0; i < N; ++i)
            {
                float s = 0;
                for (int k = 0; k < 4; ++k)
                {
                    const float fb = (float) std::pow (10.0, -3.0 * combD[k] / (0.9 * FS));
                    float& d = cb[(size_t) k][(size_t) ci[k]]; const float y = d; d = voice.sig[i] + fb * y; s += y; ci[k] = (ci[k] + 1) % combD[k];
                }
                for (int k = 0; k < 2; ++k)
                {
                    float& d = ab[(size_t) k][(size_t) ai[k]]; const float y = d - 0.5f * s; d = s + 0.5f * y; s = y; ai[k] = (ai[k] + 1) % apD[k];
                }
                rev[i] = voice.sig[i] + 0.35f * s;
            }
        }
        Rig r; r.set (ids::dv_on, 1); r.set (ids::dv_amount, 60); r.set (ids::dv_decay, 0.9f);
        auto out = r.run (rev); const size_t L = (size_t) r.latency();
        double tIn = 0, tOut = 0, vIn = 0, vOut = 0;
        for (size_t i = (size_t) (2 * FS); i + L < N; ++i)
        {
            if (voice.sig[i] == 0) { tIn += (double) rev[i] * rev[i]; tOut += (double) out[i + L] * out[i + L]; }
            if (voice.voiced[i])   { vIn += (double) voice.sig[i] * voice.sig[i]; vOut += (double) out[i + L] * out[i + L]; }
        }
        std::printf ("  reverb tail energy (gaps): reduced by %.1f dB | voiced level vs dry voice %.1f dB (reverb added %.1f dB)\n",
                     10 * std::log10 (tIn / tOut), 10 * std::log10 (vOut / vIn), rmsDb (rev, (size_t) (2 * FS), N) - rmsDb (voice.sig, (size_t) (2 * FS), N));
    }

    // 5b. de-esser
    {
        std::printf ("\n[5b] De-esser (6-9 kHz 's' bursts over the voice)\n");
        std::vector<float> in = voice.sig, ess (N, 0);
        auto hiss = noise (N, -16, 21);
        vr::Biquad bp; bp.setBandpass (FS, 7000, 1.2);
        for (size_t i = 0; i < N; ++i) { const double c = std::fmod (i / FS, 2.2); if (c > 0.7 && c < 0.85) ess[i] = bp.process (hiss[i]); }
        for (size_t i = 0; i < N; ++i) in[i] += ess[i];
        Rig r; r.set (ids::de_on, 1);
        auto outRaw = r.run (in); const size_t L = (size_t) r.latency();
        vr::Biquad h1, h2; h1.setHighpass (FS, 5000, 0.707); h2.setHighpass (FS, 5000, 0.707);
        std::vector<float> hin (N), hout (N);
        for (size_t i = 0; i < N; ++i) { hin[i] = h1.process (in[i]); hout[i] = h2.process (outRaw[i]); }
        auto& out = outRaw;
        double sIn = 0, sOut = 0, vIn = 0, vOut = 0;
        for (size_t i = (size_t) FS; i + L < N; ++i)
        {
            const double c = std::fmod (i / FS, 2.2);
            if (c > 0.72 && c < 0.83) { sIn += (double) hin[i] * hin[i]; sOut += (double) hout[i + L] * hout[i + L]; }
            if (c > 0.2 && c < 0.6)   { vIn += (double) in[i] * in[i]; vOut += (double) out[i + L] * out[i + L]; }
        }
        std::printf ("  's' band (>5k) %.1f dB quieter | vowels changed %.2f dB\n", 10 * std::log10 (sIn / sOut), 10 * std::log10 (vOut / vIn));
    }

    // 6. compressors
    {
        std::printf ("\n[6] FET + Opto\n");
        std::vector<float> in ((size_t) (6 * FS));
        for (size_t i = 0; i < in.size(); ++i)
        {
            const double t = i / FS;
            const float a = (t > 1.0 && t < 3.0) ? 0.5f : 0.05f;
            in[i] = a * (float) std::sin (2 * vr::kPi * 440 * t);
        }
        Rig r; r.set (ids::c_on, 1); r.set (ids::c_thresh, -20); r.set (ids::c_ratio, 4); r.set (ids::c_attack, 0.5f);
        r.set (ids::c_release, 100); r.set (ids::c_auto, 0);
        auto out = r.run (in); const size_t L = (size_t) r.latency();
        auto lv = [&] (const std::vector<float>& o, double t0, double t1) { return rmsDb (o, (size_t) (t0 * FS) + L, (size_t) (t1 * FS) + L); };
        std::printf ("  FET 4:1 @-20: loud section %.1f dB in -> %.1f dB out (peak detector: ~ -19.5 expected)\n", rmsDb (in, (size_t) (1.5 * FS), (size_t) (2.5 * FS)), lv (out, 1.5, 2.5));
        std::printf ("  FET attack: level 0.5..1.5 ms after onset %.1f dB, 5 ms %.1f dB\n", lv (out, 1.0005, 1.0015), lv (out, 1.004, 1.006));

        Rig o; o.set (ids::op_on, 1); o.set (ids::op_peak, 50);
        auto oo = o.run (in);
        std::printf ("  Opto: steady GR %.1f dB | recovery after release: +50ms %.1f, +300ms %.1f, +1.5s %.1f dB (quiet level %.1f)\n",
                     rmsDb (in, (size_t) (2 * FS), (size_t) (2.5 * FS)) - lv (oo, 2.0, 2.5),
                     lv (oo, 3.04, 3.06), lv (oo, 3.29, 3.31), lv (oo, 4.49, 4.51), lv (oo, 0.5, 0.9));
        Rig fo; fo.set (ids::c_on, 1); fo.set (ids::c_thresh, -20); fo.set (ids::op_on, 1); fo.set (ids::op_peak, 40);
        fo.run (in);
        std::printf ("  FET->Opto chain GR meters: FET %.1f dB, Opto %.1f dB (last block)\n", fo.p.fet.reduction.load(), fo.p.opto.reduction.load());
    }

    // 7. dynamic EQ
    {
        std::printf ("\n[7] Dynamic EQ band (1 kHz bell, thresh -30, ratio 4, range -9)\n");
        for (float lvl : { -50.0f, -20.0f, -6.0f })
        {
            std::vector<float> in ((size_t) FS);
            for (size_t i = 0; i < in.size(); ++i) in[i] = vr::dbToGain (lvl) * (float) std::sin (2 * vr::kPi * 1000 * i / FS);
            Rig r; r.set (eqId (2, "on"), 1); r.set (eqId (2, "type"), 0); r.set (eqId (2, "freq"), 1000); r.set (eqId (2, "gain"), 0);
            r.set (eqId (2, "dyn"), 1); r.set (eqId (2, "thresh"), -30); r.set (eqId (2, "ratio"), 4); r.set (eqId (2, "range"), -9);
            auto out = r.run (in);
            std::printf ("  input %5.1f dBFS -> gain change %.1f dB\n", lvl, rmsDb (out, (size_t) (0.6 * FS), N) - rmsDb (in, (size_t) (0.6 * FS), in.size()));
        }
        Rig r; r.set (eqId (4, "on"), 1); r.set (eqId (4, "type"), 0); r.set (eqId (4, "freq"), 3000); r.set (eqId (4, "gain"), 6); r.set (eqId (4, "q"), 1);
        std::vector<float> in ((size_t) FS);
        for (size_t i = 0; i < in.size(); ++i) in[i] = 0.1f * (float) std::sin (2 * vr::kPi * 3000 * i / FS);
        auto out = r.run (in);
        std::printf ("  static bell +6 dB @3k measured: %.2f dB\n", rmsDb (out, (size_t) (0.5 * FS), in.size()) - rmsDb (in, (size_t) (0.5 * FS), in.size()));
    }

    // 8. everything on: stress + CPU
    {
        std::printf ("\n[8] Full chain stress (all modules on, random automation) + CPU\n");
        Rig r;
        for (auto* id : { ids::hpf_on, ids::hum_on, ids::dc_on, ids::dp_on, ids::dn_on, ids::bg_on, ids::vi_on, ids::br_on, ids::dv_on,
                          ids::de_on, ids::c_on, ids::op_on, ids::gl_on })
            r.set (id, 1);
        for (int b = 0; b < 8; ++b) { r.set (eqId (b, "on"), 1); r.set (eqId (b, "dyn"), b % 2); }
        auto nz = noise (N, -45, 5);
        std::vector<float> in (N); for (size_t i = 0; i < N; ++i) in[i] = voice.sig[i] + nz[i];
        auto t0 = std::chrono::high_resolution_clock::now();
        auto out = r.run (in);
        auto t1 = std::chrono::high_resolution_clock::now();
        const double secs = std::chrono::duration<double> (t1 - t0).count();
        std::printf ("  20 s stereo processed in %.2f s  -> %.1f%% of one core (realtime x%.0f)\n", secs, 100 * secs / 20.0, 20.0 / secs);

        std::mt19937 rng (99);
        std::uniform_real_distribution<float> u (0, 1);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        float maxAbs = 0; bool finite = true;
        auto params = r.p.getParameters();
        for (int blk = 0; blk < 4000; ++blk)
        {
            if (blk % 8 == 0)
                for (int k = 0; k < 6; ++k)
                {
                    auto* prm = params[(int) (rng() % (unsigned) params.size())];
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                        if (rp->getParameterID() != ids::de_listen && rp->getParameterID() != ids::in_gain && rp->getParameterID() != ids::out_gain
                            && ! rp->getParameterID().endsWith ("gain") && rp->getParameterID() != ids::c_makeup && rp->getParameterID() != ids::op_gain)
                            rp->setValueNotifyingHost (u (rng));
                }
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) buf.setSample (c, i, in[(size_t) ((blk * 256 + i) % (int) N)]);
            r.p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float s = buf.getSample (c, i); if (! std::isfinite (s)) finite = false; maxAbs = std::max (maxAbs, std::abs (s)); }
        }
        std::printf ("  random automation: finite=%s, max |out| = %.2f\n", finite ? "yes" : "NO", maxAbs);
        CHECK (finite, "output must stay finite");
        // other sample rates
        for (double sr : { 44100.0, 96000.0, 192000.0 })
        {
            VoxRehabProcessor p2; p2.setPlayConfigDetails (2, 2, sr, 512); p2.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> b2 (2, 512);
            for (int k = 0; k < 200; ++k) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b2.setSample (c, i, in[(size_t) (k * 512 + i)]); p2.processBlock (b2, midi); }
            std::printf ("  %.0f Hz: latency %.1f ms, out finite=%s\n", sr, 1000.0 * p2.getLatencySamples() / sr, std::isfinite (b2.getSample (0, 100)) ? "yes" : "NO");
        }
    }

    // 9. state round trip incl. learned profile
    {
        std::printf ("\n[9] State save/restore\n");
        Rig r; r.set (ids::dn_on, 1); r.set (ids::dn_learn, 1);
        r.run (noise ((size_t) (3 * FS), -50, 4));
        r.set (ids::dn_learn, 0); r.run (std::vector<float> (2048, 0.0f));
        r.set (eqId (3, "gain"), 4.5f);
        juce::MemoryBlock mb; r.p.getStateInformation (mb);
        Rig r2; r2.p.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("  profile restored: %s, eq4 gain = %.2f\n", r2.p.hasLearnedProfile() ? "yes" : "no",
                     r2.p.apvts.getRawParameterValue (eqId (3, "gain"))->load());
        CHECK (r2.p.hasLearnedProfile(), "learned profile should persist");
        for (int i = 0; i < r.p.getNumPrograms(); ++i) r.p.setCurrentProgram (i);
        std::printf ("  %d presets load OK\n", r.p.getNumPrograms());
    }

    std::printf ("\n%s (%d failures)\n", failures == 0 ? "ALL CHECKS PASSED" : "SOME CHECKS FAILED", failures);
    return failures == 0 ? 0 : 1;
}
