// PrecisionPitchShift — DSP core: portable radix-2 FFT implementation.
#include "fft.h"

#include <cmath>

namespace pps {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

bool isPowerOfTwo(std::size_t n) { return n && ((n & (n - 1)) == 0); }

std::size_t nextPowerOfTwo(std::size_t n) {
    if (n == 0) return 1;
    --n;
    n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFFu
    n |= n >> 32;
#endif
    return n + 1;
}

void fft(std::complex<double>* data, std::size_t n, bool forward) {
    // Bit-reversal permutation.
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }
    // Danielson-Lanczos stages.
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = (forward ? -2.0 : 2.0) * kPi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (std::size_t j = 0; j < len / 2; ++j) {
                const std::complex<double> u = data[i + j];
                const std::complex<double> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (!forward) {
        const double s = 1.0 / static_cast<double>(n);
        for (std::size_t i = 0; i < n; ++i) data[i] *= s;
    }
}

void rfft(const double* in, std::complex<double>* out, std::size_t n,
          std::vector<std::complex<double>>& work) {
    if (work.size() < n) work.resize(n);
    for (std::size_t i = 0; i < n; ++i) work[i] = std::complex<double>(in[i], 0.0);
    fft(work.data(), n, true);
    for (std::size_t k = 0; k <= n / 2; ++k) out[k] = work[k];
}

void rifft(const std::complex<double>* in, double* out, std::size_t n,
           std::vector<std::complex<double>>& work) {
    if (work.size() < n) work.resize(n);
    work[0] = std::complex<double>(in[0].real(), 0.0);
    for (std::size_t k = 1; k < n / 2; ++k) work[k] = in[k];
    work[n / 2] = std::complex<double>(in[n / 2].real(), 0.0);
    for (std::size_t k = n / 2 + 1; k < n; ++k) work[k] = std::conj(work[n - k]);
    fft(work.data(), n, false);
    for (std::size_t i = 0; i < n; ++i) out[i] = work[i].real();
}

} // namespace pps
