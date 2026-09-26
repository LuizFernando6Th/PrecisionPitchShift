// PrecisionPitchShift — measurement helpers implementation.
#include "analysis.h"

#include "fft.h"
#include "spectral_processor.h"

#include <algorithm>
#include <cmath>

namespace pps::analysis {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void sine(std::vector<double>& out, double freq, double sampleRate,
          double amplitude, double phase0) {
    for (std::size_t n = 0; n < out.size(); ++n)
        out[n] = amplitude * std::sin(kPi * 2.0 * freq *
                                      static_cast<double>(n) / sampleRate +
                                      phase0);
}

void harmonicStack(std::vector<double>& out, double f0, int count,
                   double sampleRate, double amplitude) {
    std::fill(out.begin(), out.end(), 0.0);
    for (int k = 1; k <= count; ++k) {
        const double a = amplitude / static_cast<double>(k); // 1/k rolloff
        for (std::size_t n = 0; n < out.size(); ++n)
            out[n] += a * std::sin(kPi * 2.0 * f0 * static_cast<double>(k) *
                                   static_cast<double>(n) / sampleRate);
    }
    // Normalise to [-0.9, 0.9].
    double p = 0.0;
    for (double v : out) p = std::max(p, std::fabs(v));
    if (p > 0.0) {
        const double g = 0.9 / p;
        for (double& v : out) v *= g;
    }
}

void impulse(std::vector<double>& out, std::size_t pos) {
    std::fill(out.begin(), out.end(), 0.0);
    if (pos < out.size()) out[pos] = 1.0;
}

namespace {
// Magnitude spectrum (Hann) of the central portion of x.
void magSpectrum(const double* x, std::size_t n, std::vector<double>& mag,
                 std::vector<std::complex<double>>& work,
                 std::size_t skipSamples) {
    std::size_t usable = n > 2 * skipSamples ? n - 2 * skipSamples : n;
    std::size_t N = 65536;
    while (N > usable && N > 1024) N /= 2;
    if (N > usable) N = isPowerOfTwo(usable) ? usable : 1024;
    std::size_t start = (n > N) ? (n - N) / 2 : 0;
    std::vector<double> w;
    makeHannWindow(w, N);
    std::vector<double> seg(N);
    for (std::size_t i = 0; i < N; ++i) seg[i] = x[start + i] * w[i];
    work.resize(N);
    for (std::size_t i = 0; i < N; ++i) work[i] = {seg[i], 0.0};
    fft(work.data(), N, true);
    mag.resize(N / 2 + 1);
    for (std::size_t k = 0; k <= N / 2; ++k) mag[k] = std::abs(work[k]);
    // Stash N in work size for bin->Hz conversion by caller via mag.size().
}
} // namespace

double peakFrequency(const double* x, std::size_t n, double sampleRate,
                     std::size_t skipSamples) {
    std::vector<double> mag;
    std::vector<std::complex<double>> work;
    magSpectrum(x, n, mag, work, skipSamples);
    const std::size_t N = (mag.size() - 1) * 2;
    std::size_t kMax = 1;
    for (std::size_t k = 2; k + 1 < mag.size(); ++k)
        if (mag[k] > mag[kMax]) kMax = k;
    // Parabolic interpolation on log magnitude.
    double frac = 0.0;
    if (kMax > 0 && kMax + 1 < mag.size() && mag[kMax] > 0.0) {
        const double l = std::log(mag[kMax - 1] + 1e-30);
        const double c = std::log(mag[kMax] + 1e-30);
        const double r = std::log(mag[kMax + 1] + 1e-30);
        const double den = (l - 2.0 * c + r);
        if (std::fabs(den) > 1e-12) frac = 0.5 * (l - r) / den;
        if (frac > 1.0) frac = 1.0;
        if (frac < -1.0) frac = -1.0;
    }
    return (static_cast<double>(kMax) + frac) * sampleRate / static_cast<double>(N);
}

double magnitudeNear(const double* x, std::size_t n, double sampleRate,
                     double freq, std::size_t skipSamples) {
    std::vector<double> mag;
    std::vector<std::complex<double>> work;
    magSpectrum(x, n, mag, work, skipSamples);
    const std::size_t N = (mag.size() - 1) * 2;
    const double bin = freq * static_cast<double>(N) / sampleRate;
    std::size_t k0 = static_cast<std::size_t>(std::max(0.0, bin - 2.0));
    std::size_t k1 = static_cast<std::size_t>(bin + 2.0) + 1;
    if (k1 >= mag.size()) k1 = mag.size() - 1;
    double m = 0.0;
    for (std::size_t k = k0; k <= k1; ++k) m = std::max(m, mag[k]);
    return m;
}

double peakAbs(const double* x, std::size_t start, std::size_t end) {
    double p = 0.0;
    for (std::size_t i = start; i < end; ++i) p = std::max(p, std::fabs(x[i]));
    return p;
}

double rms(const double* x, std::size_t start, std::size_t end) {
    if (end <= start) return 0.0;
    double s = 0.0;
    for (std::size_t i = start; i < end; ++i) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(end - start));
}

double worstSpuriousDb(const double* x, std::size_t n, double sampleRate,
                       double f0, double toleranceHz, std::size_t skipSamples) {
    std::vector<double> mag;
    std::vector<std::complex<double>> work;
    magSpectrum(x, n, mag, work, skipSamples);
    const std::size_t N = (mag.size() - 1) * 2;
    double peak = 0.0;
    for (double m : mag) peak = std::max(peak, m);
    if (peak <= 0.0) return -200.0;
    double worst = 0.0;
    for (std::size_t k = 1; k < mag.size(); ++k) {
        const double f = static_cast<double>(k) * sampleRate / static_cast<double>(N);
        // Distance to nearest harmonic k*f0.
        const double q = f / f0;
        const double nearest = std::round(q) * f0;
        if (std::fabs(f - nearest) <= toleranceHz) continue;
        if (mag[k] > worst) worst = mag[k];
    }
    return 20.0 * std::log10(worst / peak + 1e-30);
}

} // namespace pps::analysis
