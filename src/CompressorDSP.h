#pragma once

// SixThirty DSP core -- a model of the Alesis 3630 compressor/limiter signal flow.
// No JUCE dependency, so it can be unit-tested on its own (see tests/dsp_tests.cpp).
//
// Signal flow modelled after the 3630 service manual (section 3, "Signal Paths"):
//
//   input buffer --+--------------------------> VCA (THAT 2150 style, dB-linear) --> output gain --> out
//                  |                              ^
//                  +--> detector (2252 RMS or peak)  |
//                        -> threshold rectifier      |
//                        -> ratio / hard-soft knee   |
//                        -> attack/release shaping --+  (+ gate detector, summed into the same control)
//
// The detector is fed from the input buffer, i.e. the compressor is feed-forward.
// Stereo link derives one control signal from both channels, applied to both.

#include <algorithm>
#include <cmath>

namespace sixthirty
{
// 3630 spec sheet: headroom is +18 dBu, so we treat 0 dBFS as +18 dBu.
constexpr float kFullScaleDbu = 18.0f;
// The -10 dBV position raises the input stage gain by the difference between
// +4 dBu (1.228 V) and -10 dBV (0.316 V); the output stage takes it back off.
// Net audio gain is unity, but the detector sees a hotter signal.
constexpr float kMinus10Boost = 11.78f;
// Ratio knob fully clockwise means infinity:1 (limiting).
constexpr float kInfRatio = 20.0f;
// Gate threshold knob fully counter-clockwise = gate off.
constexpr float kGateOffDbu = -79.5f;
// Things the public documentation does not pin down; chosen here:
constexpr float kGateDepthDb = 60.0f;  // how far a closed gate attenuates
constexpr float kSoftKneeDb = 10.0f;   // width of the soft-knee region

struct Params
{
    float thresholdDbu = 0.0f;       // -40 .. +20
    float ratio = 4.0f;              // 1 .. kInfRatio (max = infinity)
    float attackMs = 5.0f;           // 0.1 .. 200   (peak mode only)
    float releaseMs = 300.0f;        // 50 .. 3000   (peak mode only)
    float outputDb = 0.0f;           // -20 .. +20
    float gateThresholdDbu = -80.0f; // <= kGateOffDbu disables the gate, up to -10
    float gateRateMs = 200.0f;       // 20 .. 2000
    bool rms = false;                // false = peak detector, true = RMS (program dependent)
    bool softKnee = true;
    bool minus10dBV = false;         // false = +4 dBu
    bool stereoLink = true;
    bool bypass = false;
};

class Compressor
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        peakDecay = coef (0.008f);
        msCoef = coef (0.025f);
        bypassStep = 1.0f - coef (0.005f);
        outSmooth = 1.0f - coef (0.010f);
        reset();
        setParams (params);
        outDb = params.outputDb;
        bypassMix = params.bypass ? 0.0f : 1.0f;
    }

    void reset()
    {
        for (int c = 0; c < kMaxChannels; ++c)
        {
            peak[c] = 0.0f;
            ms[c] = 0.0f;
            env[c] = 0.0f;
            gateDb[c] = 0.0f;
            gateOpen[c] = true;
        }
        meterGr = 0.0f;
        gateClosedFlag = false;
    }

    void setParams (const Params& np)
    {
        params = np;
        const float atk = np.rms ? 0.001f : np.attackMs * 0.001f;
        const float rel = np.rms ? 0.010f : np.releaseMs * 0.001f;
        atkCoef = coef (atk);
        relCoef = coef (rel);
        slope = np.ratio >= kInfRatio ? 1.0f : 1.0f - 1.0f / std::max (np.ratio, 1.0f);
        thrDb = np.thresholdDbu - kFullScaleDbu;
        gateEnabled = np.gateThresholdDbu > kGateOffDbu;
        gateThrDb = np.gateThresholdDbu - kFullScaleDbu;
        gateCloseStep = kGateDepthDb / std::max (1.0f, np.gateRateMs * 0.001f * fs);
        gateOpenStep = kGateDepthDb / std::max (1.0f, 0.0005f * fs);
        detBoost = np.minus10dBV ? kMinus10Boost : 0.0f;
    }

    // audio:  in/out, processed in place.
    // det:    optional external sidechain (nullptr = detect from the audio input).
    void process (float* const* audio, int numCh, const float* const* det, int numDetCh, int numSamples)
    {
        numCh = std::min (numCh, kMaxChannels);
        if (numCh <= 0 || numSamples <= 0)
            return;
        numDetCh = std::max (numDetCh, 1);
        const int ctrlCh = params.stereoLink ? 1 : numCh;
        const float bypassTarget = params.bypass ? 0.0f : 1.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            float lvl[kMaxChannels] = { -200.0f, -200.0f };
            float gLvl[kMaxChannels] = { -200.0f, -200.0f };

            for (int c = 0; c < numCh; ++c)
            {
                const float x = det != nullptr ? det[std::min (c, numDetCh - 1)][i] : audio[c][i];
                peak[c] = std::max (std::fabs (x), peak[c] * peakDecay);
                ms[c] += (1.0f - msCoef) * (x * x - ms[c]);
                if (ms[c] < 1.0e-18f)
                    ms[c] = 0.0f;

                const float pkDb = 20.0f * std::log10 (peak[c] + 1.0e-9f);
                const float rmsDb = 10.0f * std::log10 (ms[c] + 1.0e-18f);
                lvl[c] = (params.rms ? rmsDb : pkDb) + detBoost;
                gLvl[c] = pkDb + detBoost;
            }

            if (params.stereoLink && numCh > 1)
            {
                lvl[0] = std::max (lvl[0], lvl[1]);
                gLvl[0] = std::max (gLvl[0], gLvl[1]);
            }

            for (int k = 0; k < ctrlCh; ++k)
            {
                // threshold rectifier + ratio + knee -> wanted gain reduction in dB
                const float gr = gainComputer (lvl[k]);

                // envelope shaping (attack/release RC networks)
                float c;
                if (params.rms)
                {
                    // Program-dependent timing: bigger jumps attack faster,
                    // deeper compression releases slower.
                    const float tauA = 0.025f / (1.0f + std::max (0.0f, gr - env[k]) * 0.2f);
                    const float tauR = 0.060f + 0.006f * env[k];
                    c = coef (gr > env[k] ? tauA : tauR);
                }
                else
                {
                    c = gr > env[k] ? atkCoef : relCoef;
                }
                env[k] = gr + c * (env[k] - gr);

                // gate detector (always peak based): fades the VCA down below threshold
                if (gateEnabled)
                {
                    if (gateOpen[k] && gLvl[k] < gateThrDb - 1.0f)
                        gateOpen[k] = false;
                    else if (! gateOpen[k] && gLvl[k] > gateThrDb + 1.0f)
                        gateOpen[k] = true;
                }
                else
                {
                    gateOpen[k] = true;
                }

                if (gateOpen[k])
                    gateDb[k] = std::min (0.0f, gateDb[k] + gateOpenStep);
                else
                    gateDb[k] = std::max (-kGateDepthDb, gateDb[k] - gateCloseStep);
            }

            outDb += (params.outputDb - outDb) * outSmooth;
            bypassMix += (bypassTarget - bypassMix) * bypassStep;

            for (int c = 0; c < numCh; ++c)
            {
                const int k = params.stereoLink ? 0 : c;
                const float gDb = std::clamp (-env[k] + gateDb[k] + outDb, -140.0f, 60.0f);
                const float dry = audio[c][i];
                const float wet = dry * std::exp (gDb * 0.115129255f);
                audio[c][i] = dry + bypassMix * (wet - dry);
            }
        }

        float maxGr = 0.0f;
        bool anyClosed = false;
        for (int k = 0; k < ctrlCh; ++k)
        {
            maxGr = std::max (maxGr, env[k]);
            anyClosed = anyClosed || (gateEnabled && ! gateOpen[k]);
        }
        meterGr = bypassMix > 0.5f ? maxGr : 0.0f;
        gateClosedFlag = anyClosed;
    }

    float getGainReductionDb() const { return meterGr; }
    bool isGateClosed() const { return gateClosedFlag; }

private:
    float coef (float tauSec) const { return std::exp (-1.0f / (std::max (tauSec, 1.0e-6f) * fs)); }

    float gainComputer (float levelDb) const
    {
        const float over = levelDb - thrDb;
        if (! params.softKnee)
            return over > 0.0f ? over * slope : 0.0f;

        const float w = kSoftKneeDb;
        if (over <= -0.5f * w)
            return 0.0f;
        if (over >= 0.5f * w)
            return over * slope;
        const float t = over + 0.5f * w;
        return slope * t * t / (2.0f * w);
    }

    Params params;
    float fs = 48000.0f;

    float peakDecay = 0.0f, msCoef = 0.0f, atkCoef = 0.0f, relCoef = 0.0f;
    float bypassStep = 0.0f, outSmooth = 0.0f;
    float slope = 0.75f, thrDb = -18.0f, detBoost = 0.0f;
    float gateThrDb = -100.0f, gateCloseStep = 0.0f, gateOpenStep = 0.0f;
    bool gateEnabled = false;

    float peak[kMaxChannels] = {};
    float ms[kMaxChannels] = {};
    float env[kMaxChannels] = {};
    float gateDb[kMaxChannels] = {};
    bool gateOpen[kMaxChannels] = { true, true };
    float outDb = 0.0f;
    float bypassMix = 1.0f;

    float meterGr = 0.0f;
    bool gateClosedFlag = false;
};
} // namespace sixthirty
