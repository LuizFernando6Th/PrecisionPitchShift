// PrecisionPitchShift — transparent output gain protection.
//
// This is NOT a compressor/limiter: it applies a single scalar linear gain
// (no ratio, knee, attack/release envelope shaping of dynamics). Two modes:
//
//  * LatchedGlobal: remembers the largest peak seen since reset(); gain only
//    ever decreases towards ceiling/peakMax. In an offline render this is
//    equivalent to a post-hoc global gain adjustment, preserving relative
//    dynamics exactly. Gain changes are ramped over a few ms to avoid clicks.
//  * Causal (streaming): same latch, applied causally. Early portions of a
//    file may be louder than later ones if a bigger peak appears later.
//    This limitation is documented; for critical masters, do a full playback
//    pass (or render once to measure, then render again) — or disable and
//    adjust manually.
//
// Default ceiling: -1.0 dBFS.
#pragma once

#include <algorithm>
#include <cmath>

namespace pps {

class GainProtection {
public:
    explicit GainProtection(double ceilingDb = -1.0, double rampMs = 5.0,
                            double sampleRate = 48000.0)
        : ceilingDb_(ceilingDb), rampMs_(rampMs), sampleRate_(sampleRate) {
        reset();
    }

    void reset() {
        peakMax_ = 0.0;
        currentGain_ = 1.0;
        targetGain_ = 1.0;
    }

    void setSampleRate(double sr) { sampleRate_ = sr > 0.0 ? sr : 48000.0; }
    void setCeilingDb(double db) { ceilingDb_ = db; recompute(); }
    void setRampMs(double ms) { rampMs_ = ms < 0.5 ? 0.5 : ms; }

    double ceilingDb() const { return ceilingDb_; }
    double currentGain() const { return currentGain_; }
    double peakMax() const { return peakMax_; }

    // Observe an output block (pre-gain). Updates the latched peak/target.
    void observe(const double* const* channels, int numChannels, int numSamples) {
        double peak = peakMax_;
        for (int c = 0; c < numChannels; ++c) {
            const double* x = channels[c];
            for (int n = 0; n < numSamples; ++n) {
                const double a = std::fabs(x[n]);
                if (a > peak) peak = a;
            }
        }
        if (peak > peakMax_) {
            peakMax_ = peak;
            recompute();
        }
    }

    // Apply current gain in place with click-free ramping towards target.
    void apply(double** channels, int numChannels, int numSamples) {
        if (numSamples <= 0) return;
        const double rampSamples = rampMs_ * 0.001 * sampleRate_;
        const int R = rampSamples < 1.0 ? 1 : static_cast<int>(rampSamples);
        for (int n = 0; n < numSamples; ++n) {
            if (currentGain_ != targetGain_) {
                const double step = (targetGain_ - currentGain_) /
                                    static_cast<double>(R);
                // Move at most one ramp-step per sample; snap when close.
                if (std::fabs(targetGain_ - currentGain_) <= std::fabs(step))
                    currentGain_ = targetGain_;
                else
                    currentGain_ += step;
            }
            for (int c = 0; c < numChannels; ++c) channels[c][n] *= currentGain_;
        }
    }

    static double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

private:
    void recompute() {
        const double ceiling = dbToLinear(ceilingDb_);
        targetGain_ = (peakMax_ > ceiling && peakMax_ > 0.0)
                          ? ceiling / peakMax_
                          : 1.0;
        if (targetGain_ > 1.0) targetGain_ = 1.0;
    }

    double ceilingDb_ = -1.0;
    double rampMs_ = 5.0;
    double sampleRate_ = 48000.0;
    double peakMax_ = 0.0;
    double currentGain_ = 1.0;
    double targetGain_ = 1.0;
};

} // namespace pps
