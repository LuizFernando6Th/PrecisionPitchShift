// PrecisionPitchShift — anti-alias / Nyquist-guard implementation.
#include "alias_protection.h"

#include <cmath>

namespace pps {

void computeAliasGuard(std::vector<double>& gains, std::size_t fftSize,
                       double factor, double fadeFraction) {
    const std::size_t numBins = fftSize / 2 + 1;
    gains.assign(numBins, 1.0);
    if (factor <= 1.0 || factor != factor) return; // NaN-safe: no guard
    const double cutoffJ = (static_cast<double>(fftSize) * 0.5) / factor;
    double fadeBins = fadeFraction * static_cast<double>(numBins);
    if (fadeBins < 1.0) fadeBins = 1.0;
    const double knee = cutoffJ - fadeBins;
    constexpr double kPi = 3.14159265358979323846;
    for (std::size_t j = 0; j < numBins; ++j) {
        const double jd = static_cast<double>(j);
        if (jd <= knee) {
            gains[j] = 1.0;
        } else if (jd >= cutoffJ) {
            gains[j] = 0.0;
        } else {
            const double t = (jd - knee) / fadeBins; // 0..1
            gains[j] = 0.5 + 0.5 * std::cos(kPi * t); // raised cosine 1->0
        }
    }
}

double survivingInputBandwidth(double sampleRate, double factor) {
    const double nyquist = sampleRate * 0.5;
    if (factor <= 1.0) return nyquist;
    return nyquist / factor;
}

} // namespace pps
