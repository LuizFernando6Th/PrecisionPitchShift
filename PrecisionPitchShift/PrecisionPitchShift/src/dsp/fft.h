// PrecisionPitchShift — DSP core: portable radix-2 FFT (float64).
// Self-contained (no third-party DSP dependency) so the full Nyquist band
// behaviour is auditable and there is no hidden fixed 20 kHz low-pass.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace pps {

// In-place iterative Cooley-Tukey FFT for power-of-two sizes.
// forward=true  -> forward transform (no scaling)
// forward=false -> inverse transform (scales by 1/N)
void fft(std::complex<double>* data, std::size_t n, bool forward);

bool isPowerOfTwo(std::size_t n);
std::size_t nextPowerOfTwo(std::size_t n);

// Real forward transform: in[N] real -> out[N/2+1] complex.
// Real inverse transform: in[N/2+1] complex -> out[N] real.
void rfft(const double* in, std::complex<double>* out, std::size_t n,
          std::vector<std::complex<double>>& work);
void rifft(const std::complex<double>* in, double* out, std::size_t n,
           std::vector<std::complex<double>>& work);

} // namespace pps
