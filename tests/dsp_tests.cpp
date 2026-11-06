// Standalone checks of the compressor core. Build: g++ -O2 -std=c++17 -I../src dsp_tests.cpp
#include "CompressorDSP.h"

#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

using namespace sixthirty;

static constexpr double kFs = 48000.0;
static int failures = 0;
static int checks = 0;

static void check (bool ok, const char* name, double got, const char* expect)
{
    ++checks;
    std::printf ("  [%s] %-46s got %10.3f   expected %s\n", ok ? "PASS" : "FAIL", name, got, expect);
    if (! ok)
        ++failures;
}

static double dbfs (double lin) { return 20.0 * std::log10 (std::max (lin, 1.0e-12)); }

static Params base()
{
    Params p;
    p.thresholdDbu = 0.0f; // -18 dBFS
    p.ratio = 4.0f;
    p.attackMs = 0.1f;
    p.releaseMs = 50.0f;
    p.softKnee = false;
    p.stereoLink = false;
    p.outputDb = 0.0f;
    return p;
}

// Runs a stereo sine through the compressor, returns peak of the last `tailMs`.
static void runSine (Compressor& c, double ampL, double ampR, double seconds, double tailMs,
                     double& peakL, double& peakR, double freq = 1000.0)
{
    const int n = (int) (seconds * kFs);
    const int tail = (int) (tailMs * 0.001 * kFs);
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i)
    {
        const double s = std::sin (2.0 * M_PI * freq * i / kFs);
        l[i] = (float) (ampL * s);
        r[i] = (float) (ampR * s);
    }
    float* ch[2] = { l.data(), r.data() };
    const int block = 64;
    for (int i = 0; i < n; i += block)
    {
        float* b[2] = { l.data() + i, r.data() + i };
        c.process (b, 2, nullptr, 2, std::min (block, n - i));
    }
    peakL = peakR = 0.0;
    for (int i = n - tail; i < n; ++i)
    {
        peakL = std::max (peakL, (double) std::fabs (ch[0][i]));
        peakR = std::max (peakR, (double) std::fabs (ch[1][i]));
    }
}

static double steadyOutDb (const Params& p, double inDb, double seconds = 1.0)
{
    Compressor c;
    c.prepare (kFs);
    c.setParams (p);
    double pl, pr;
    runSine (c, std::pow (10.0, inDb / 20.0), 0.0, seconds, 100.0, pl, pr);
    return dbfs (pl);
}

int main()
{
    std::printf ("Static curve\n");
    {
        Params p = base();
        check (std::fabs (steadyOutDb (p, -30.0) - -30.0) < 0.2, "below threshold passes unchanged", steadyOutDb (p, -30.0), "-30.0 +-0.2");
        double o = steadyOutDb (p, -6.0);
        check (std::fabs (o - -15.0) < 0.5, "hard knee 4:1, 12 dB over -> 9 dB GR", o, "-15.0 +-0.5");
        p.ratio = kInfRatio;
        o = steadyOutDb (p, -6.0);
        check (std::fabs (o - -18.0) < 0.5, "infinity:1 clamps to threshold", o, "-18.0 +-0.5");
        p.ratio = 1.0f;
        o = steadyOutDb (p, -6.0);
        check (std::fabs (o - -6.0) < 0.2, "1:1 is transparent", o, "-6.0 +-0.2");
        p.outputDb = 6.0f;
        o = steadyOutDb (p, -6.0);
        check (std::fabs (o - 0.0) < 0.3, "output gain +6 dB at 1:1", o, "0.0 +-0.3");
    }

    std::printf ("Soft knee\n");
    {
        Params p = base();
        p.softKnee = true;
        double o = steadyOutDb (p, -18.0);
        check (std::fabs (o - (-18.0 - 0.9375)) < 0.35, "at threshold: slope*W/8 = 0.94 dB GR", o, "-18.94 +-0.35");
        o = steadyOutDb (p, -24.0);
        check (std::fabs (o - -24.0) < 0.2, "knee starts 5 dB below threshold", o, "-24.0 +-0.2");
        o = steadyOutDb (p, -6.0);
        check (std::fabs (o - -15.0) < 0.5, "well above knee matches hard knee", o, "-15.0 +-0.5");
    }

    std::printf ("Level calibration\n");
    {
        Params p = base();
        p.minus10dBV = true;
        double o = steadyOutDb (p, -20.0);
        // detector sees -8.22 dBFS -> 9.78 over -> 7.34 dB GR
        check (std::fabs (o - (-20.0 - 7.335)) < 0.5, "-10 dBV mode drives detector 11.78 dB hotter", o, "-27.3 +-0.5");
    }

    std::printf ("RMS mode\n");
    {
        Params p = base();
        p.rms = true;
        double o = steadyOutDb (p, -6.0, 1.5);
        // sine RMS = peak - 3 dB -> 9 dB over -> 6.75 dB GR
        check (std::fabs (o - (-6.0 - 6.75)) < 0.4, "RMS reads sine 3 dB below peak", o, "-12.75 +-0.4");

        Compressor a, b;
        Params pa = p, pb = p;
        pa.attackMs = 0.1f; pa.releaseMs = 50.0f;
        pb.attackMs = 200.0f; pb.releaseMs = 3000.0f;
        a.prepare (kFs); b.prepare (kFs);
        a.setParams (pa); b.setParams (pb);
        std::vector<float> x (48000), y;
        std::mt19937 rng (1);
        std::uniform_real_distribution<float> d (-0.5f, 0.5f);
        for (auto& s : x) s = d (rng);
        y = x;
        float* ca[1] = { x.data() }; float* cb[1] = { y.data() };
        a.process (ca, 1, nullptr, 1, (int) x.size());
        b.process (cb, 1, nullptr, 1, (int) y.size());
        double maxDiff = 0.0;
        for (size_t i = 0; i < x.size(); ++i) maxDiff = std::max (maxDiff, (double) std::fabs (x[i] - y[i]));
        check (maxDiff == 0.0, "attack/release knobs inert in RMS mode", maxDiff, "0 (identical output)");
    }

    std::printf ("Attack / release timing (peak mode)\n");
    {
        Params p = base();
        p.thresholdDbu = -12.0f; // -30 dBFS
        p.ratio = kInfRatio;
        p.attackMs = 20.0f;
        p.releaseMs = 500.0f;
        Compressor c;
        c.prepare (kFs);
        c.setParams (p);

        auto feed = [&] (double ampDb, double ms, std::vector<float>* grTrace)
        {
            const int n = (int) (ms * 0.001 * kFs);
            const double amp = std::pow (10.0, ampDb / 20.0);
            static double phase = 0.0;
            for (int i = 0; i < n; i += 16)
            {
                float buf[16];
                const int m = std::min (16, n - i);
                for (int j = 0; j < m; ++j)
                {
                    buf[j] = (float) (amp * std::sin (phase));
                    phase += 2.0 * M_PI * 1000.0 / kFs;
                }
                float* ch[1] = { buf };
                c.process (ch, 1, nullptr, 1, m);
                if (grTrace) for (int j = 0; j < m; ++j) grTrace->push_back (c.getGainReductionDb());
            }
        };

        feed (-60.0, 200.0, nullptr);
        std::vector<float> atk;
        feed (-10.0, 400.0, &atk);
        const double final = atk.back();
        size_t t63 = 0;
        while (t63 < atk.size() && atk[t63] < 0.632 * final) ++t63;
        const double atkMs = 1000.0 * (double) t63 / kFs;
        check (std::fabs (final - 20.0) < 0.5, "steady GR for 20 dB over, infinity:1", final, "20.0 +-0.5");
        check (atkMs > 18.0 && atkMs < 23.0, "attack time constant (63%) with knob at 20 ms", atkMs, "18..23 ms");

        std::vector<float> rel;
        feed (-70.0, 2500.0, &rel);
        size_t t37 = 0;
        while (t37 < rel.size() && rel[t37] > 0.368 * final) ++t37;
        const double relMs = 1000.0 * (double) t37 / kFs;
        check (relMs > 480.0 && relMs < 545.0, "release time constant (37%) with knob at 500 ms", relMs, "480..545 ms");
    }

    std::printf ("Noise gate\n");
    {
        Params p = base();
        p.thresholdDbu = 20.0f;
        p.ratio = 1.0f;
        p.gateThresholdDbu = -30.0f; // -48 dBFS
        p.gateRateMs = 100.0f;
        double o = steadyOutDb (p, -30.0);
        check (std::fabs (o - -30.0) < 0.3, "signal above gate threshold passes", o, "-30.0 +-0.3");
        o = steadyOutDb (p, -60.0, 1.0);
        check (o < -100.0, "signal below gate threshold is gated", o, "< -100 dBFS");

        p.gateThresholdDbu = -80.0f; // knob CCW = off
        o = steadyOutDb (p, -60.0, 1.0);
        check (std::fabs (o - -60.0) < 0.3, "gate off: quiet signal passes", o, "-60.0 +-0.3");

        // Fade shape: loud -> quiet, rate 500 ms, linear-in-dB fade of 60 dB.
        p.gateThresholdDbu = -30.0f;
        p.gateRateMs = 500.0f;
        Compressor c;
        c.prepare (kFs);
        c.setParams (p);
        double pl, pr;
        runSine (c, std::pow (10.0, -30.0 / 20.0), 0.0, 0.5, 50.0, pl, pr);
        runSine (c, std::pow (10.0, -60.0 / 20.0), 0.0, 0.250, 20.0, pl, pr);
        double mid = dbfs (pl);
        // 60 dB over 500 ms = 0.12 dB/ms; the gate starts closing ~17 ms in (peak hold decay) and the
        // measurement window is the last 20 ms, so roughly -25 dB is expected here.
        check (mid > -100.0 && mid < -80.0, "gate fade is a ramp (attenuation at ~250 ms)", mid - -60.0, "-36..-20 dB vs input");
    }

    std::printf ("Stereo link\n");
    {
        Params p = base();
        p.stereoLink = true;
        Compressor c;
        c.prepare (kFs);
        c.setParams (p);
        double pl, pr;
        runSine (c, std::pow (10.0, -6.0 / 20.0), std::pow (10.0, -40.0 / 20.0), 1.0, 100.0, pl, pr);
        check (std::fabs (dbfs (pl) - -15.0) < 0.5, "linked: loud channel compressed", dbfs (pl), "-15.0 +-0.5");
        check (std::fabs (dbfs (pr) - -49.0) < 0.5, "linked: quiet channel gets the same GR", dbfs (pr), "-49.0 +-0.5");

        p.stereoLink = false;
        Compressor u;
        u.prepare (kFs);
        u.setParams (p);
        runSine (u, std::pow (10.0, -6.0 / 20.0), std::pow (10.0, -40.0 / 20.0), 1.0, 100.0, pl, pr);
        check (std::fabs (dbfs (pr) - -40.0) < 0.3, "unlinked: quiet channel untouched", dbfs (pr), "-40.0 +-0.3");
    }

    std::printf ("Bypass\n");
    {
        Params p = base();
        Compressor c;
        c.prepare (kFs);
        c.setParams (p);
        double pl, pr;
        runSine (c, 0.5, 0.5, 0.2, 10.0, pl, pr);
        const double grBefore = c.getGainReductionDb();
        check (grBefore > 1.0, "compressing before bypass (GR meter)", grBefore, "> 1 dB");
        p.bypass = true;
        c.setParams (p);
        runSine (c, 0.5, 0.5, 0.2, 10.0, pl, pr);
        check (std::fabs (dbfs (pl) - dbfs (0.5)) < 0.05, "bypassed output equals input", dbfs (pl), "-6.02 +-0.05");
        check (c.getGainReductionDb() == 0.0f, "bypassed GR meter reads 0", c.getGainReductionDb(), "0");
    }

    std::printf ("Robustness\n");
    {
        std::mt19937 rng (7);
        std::uniform_real_distribution<float> d (-1.0f, 1.0f);
        bool finite = true;
        double worst = 0.0;
        for (int trial = 0; trial < 24; ++trial)
        {
            Params p;
            p.thresholdDbu = -40.0f + 60.0f * (trial % 5) / 4.0f;
            p.ratio = (trial % 3 == 0) ? kInfRatio : 1.0f + 19.0f * d (rng) * d (rng);
            p.ratio = std::clamp (std::fabs (p.ratio), 1.0f, kInfRatio);
            p.attackMs = (trial % 2) ? 0.1f : 200.0f;
            p.releaseMs = (trial % 4 < 2) ? 50.0f : 3000.0f;
            p.outputDb = (trial % 2) ? 20.0f : -20.0f;
            p.gateThresholdDbu = (trial % 3) ? -10.0f : -80.0f;
            p.gateRateMs = (trial % 2) ? 20.0f : 2000.0f;
            p.rms = trial % 2 == 0;
            p.softKnee = trial % 3 != 0;
            p.minus10dBV = trial % 4 == 0;
            p.stereoLink = trial % 2 == 1;
            Compressor c;
            c.prepare (trial % 2 ? 44100.0 : 96000.0);
            c.setParams (p);
            std::vector<float> l (20000), r (20000);
            for (int i = 0; i < 20000; ++i)
            {
                const float burst = (i / 4000) % 2 ? 0.0f : 1.0f;
                l[i] = burst * d (rng);
                r[i] = burst * d (rng);
            }
            float* ch[2] = { l.data(), r.data() };
            c.process (ch, 2, nullptr, 2, 20000);
            for (int i = 0; i < 20000; ++i)
            {
                if (! std::isfinite (l[i]) || ! std::isfinite (r[i])) finite = false;
                worst = std::max (worst, (double) std::max (std::fabs (l[i]), std::fabs (r[i])));
            }
        }
        check (finite, "24 extreme-setting noise runs stay finite", finite ? 1.0 : 0.0, "all finite");
        check (worst < 20.0, "output stays bounded (+20 dB makeup worst case)", worst, "< 20");
    }

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
