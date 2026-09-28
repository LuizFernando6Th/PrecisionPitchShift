// PrecisionPitchShift — spectral helpers implementation.
#include "spectral_processor.h"
#include "phase_processor.h"

#include <cmath>

namespace pps {

void makeHannWindow(std::vector<double>& w, std::size_t n) {
    w.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        w[i] = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) /
                                    static_cast<double>(n));
}

void makeBlackmanHarrisWindow(std::vector<double>& w, std::size_t n) {
    w.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double p = kTwoPi * static_cast<double>(i) / static_cast<double>(n);
        w[i] = 0.35875 - 0.48829 * std::cos(p) + 0.14128 * std::cos(2.0 * p) -
               0.01168 * std::cos(3.0 * p);
    }
}

void makeWindow(std::vector<double>& w, std::size_t n, WindowType t) {
    if (t == WindowType::BlackmanHarris)
        makeBlackmanHarrisWindow(w, n);
    else
        makeHannWindow(w, n);
}

double meanSquare(const std::vector<double>& w) {
    if (w.empty()) return 1.0;
    double s = 0.0;
    for (double v : w) s += v * v;
    return s / static_cast<double>(w.size());
}

void estimateTrueFrequencies(const double* anaPhase, const double* prevAna,
                             double* trueFreq, std::size_t numBins,
                             std::size_t fftSize, int hop) {
    const double omegaStep = kTwoPi * static_cast<double>(hop) /
                             static_cast<double>(fftSize);
    for (std::size_t k = 0; k < numBins; ++k) {
        const double omega = omegaStep * static_cast<double>(k);
        const double delta = wrapPi(anaPhase[k] - prevAna[k] - omega);
        trueFreq[k] =
            (omega + delta) / static_cast<double>(hop); // rad per sample
    }
}

double timeCrest(const double* frame, std::size_t n) {
    if (n == 0) return 0.0;
    double pk = 0.0, s = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double a = std::fabs(frame[i]);
        if (a > pk) pk = a;
        s += frame[i] * frame[i];
    }
    const double rms = std::sqrt(s / static_cast<double>(n));
    if (rms <= 1e-30) return 0.0; // digital silence: not a transient
    return pk / rms;
}

// (Identity phase locking: see phase_processor.cpp — applyIdentityPhaseLock.)

} // namespace pps
