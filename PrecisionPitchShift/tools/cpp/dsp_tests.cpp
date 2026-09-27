// PrecisionPitchShift — DSP test runner (no third-party framework).
// Builds with any C++17 compiler, e.g.:
//   g++ -O2 -std=c++17 -Isrc tests/cpp/dsp_tests.cpp src/dsp/*.cpp -o dsp_tests
// Real measured numbers are printed; thresholds below are pass/fail gates.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "dsp/alias_protection.h"
#include "dsp/analysis.h"
#include "dsp/fft.h"
#include "dsp/gain_control.h"
#include "dsp/pitch_engine.h"
#include "plugin/parameters.h"

namespace {
int g_fail = 0;
int g_pass = 0;

void check(bool cond, const std::string& name, const std::string& detail = "") {
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s %s\n", name.c_str(), detail.c_str());
    } else {
        ++g_fail;
        std::printf("[FAIL] %s %s\n", name.c_str(), detail.c_str());
    }
}

char buf[256];

// Run the streaming engine over `in` (mono or stereo interleaved as vector
// per channel) in host-like blocks; returns output per channel.
std::vector<std::vector<double>> runEngine(const std::vector<std::vector<double>>& in,
                                           double sampleRate, double factor,
                                           int blockSize = 512,
                                           pps::QualityMode q = pps::QualityMode::HighPrecision,
                                           bool phaseLock = true) {
    pps::PitchEngine eng;
    pps::EngineConfig cfg;
    cfg.sampleRate = sampleRate;
    cfg.numChannels = static_cast<int>(in.size());
    cfg.factor = factor;
    cfg.quality = q;
    cfg.phaseLock = phaseLock;
    if (!eng.configure(cfg)) return {};
    const std::size_t n = in[0].size();
    std::vector<std::vector<double>> out(in.size(), std::vector<double>(n, 0.0));
    std::vector<const double*> ip(in.size());
    std::vector<double*> op(in.size());
    for (std::size_t off = 0; off < n; off += static_cast<std::size_t>(blockSize)) {
        const int m = static_cast<int>(std::min<std::size_t>(blockSize, n - off));
        for (std::size_t c = 0; c < in.size(); ++c) {
            ip[c] = in[c].data() + off;
            op[c] = out[c].data() + off;
        }
        eng.process(ip.data(), op.data(), m);
    }
    return out;
}

std::vector<double> renderMono(const std::vector<double>& in, double sr, double factor,
                               int block = 512, bool phaseLock = true) {
    auto out = runEngine({in}, sr, factor, block, pps::QualityMode::HighPrecision,
                         phaseLock);
    return out.empty() ? std::vector<double>() : out[0];
}
} // namespace

int main() {
    using namespace pps;

    // --- 1. Factor math ------------------------------------------------
    {
        const double f = params::factor(440.0, 444.0);
        std::snprintf(buf, sizeof(buf), "(440->444 = %.9f)", f);
        check(std::fabs(f - 1.009090909090909) < 1e-12, "factor 440->444", buf);
        check(std::fabs(params::factor(432.0, 440.0) - 440.0 / 432.0) < 1e-12,
              "factor 432->440");
        check(std::fabs(params::freqFromNorm(params::freqToNorm(440.01)) - 440.01) < 1e-9,
              "freq norm roundtrip 0.01Hz");
    }

    // --- 2. FFT roundtrip ----------------------------------------------
    {
        const std::size_t N = 4096;
        std::vector<double> x(N), y(N);
        for (std::size_t n = 0; n < N; ++n)
            x[n] = std::sin(2 * 3.14159265358979 * 440.0 * n / 48000.0) +
                   0.5 * std::cos(2 * 3.14159265358979 * 1234.0 * n / 48000.0);
        std::vector<std::complex<double>> spec(N / 2 + 1), work;
        rfft(x.data(), spec.data(), N, work);
        rifft(spec.data(), y.data(), N, work);
        double err = 0.0;
        for (std::size_t n = 0; n < N; ++n) err = std::max(err, std::fabs(x[n] - y[n]));
        std::snprintf(buf, sizeof(buf), "(max err=%.3e)", err);
        check(err < 1e-9, "fft roundtrip", buf);
    }

    // --- 3. Alias guard shape -------------------------------------------
    {
        std::vector<double> g;
        computeAliasGuard(g, 4096, 1.0);
        bool allOne = true;
        for (double v : g) if (v != 1.0) allOne = false;
        check(allOne, "guard factor<=1 is unity");

        computeAliasGuard(g, 4096, 444.0 / 440.0);
        const double cutoffJ = 2048.0 / (444.0 / 440.0); // ~2029.7
        bool ok = g[0] == 1.0 && g[1000] == 1.0 && g[2047] == 0.0 && g[2048] == 0.0;
        // smooth transition: strictly between 0 and 1 just below cutoff
        bool smooth = g[2025] < 1.0 && g[2025] > 0.0;
        std::snprintf(buf, sizeof(buf), "(cutoffJ=%.1f g[2025]=%.3f)", cutoffJ, g[2025]);
        check(ok && smooth, "guard upward-shift shape", buf);
        check(survivingInputBandwidth(48000.0, 444.0 / 440.0) < 24000.0 &&
                  survivingInputBandwidth(48000.0, 0.5) == 24000.0,
              "surviving bandwidth");
    }

    // --- 4. FFT size policy ----------------------------------------------
    {
        check(suggestedFftSize(44100, QualityMode::HighPrecision) == 4096, "fft 44.1k");
        check(suggestedFftSize(48000, QualityMode::HighPrecision) == 4096, "fft 48k");
        check(suggestedFftSize(96000, QualityMode::HighPrecision) == 8192, "fft 96k");
        check(suggestedFftSize(192000, QualityMode::HighPrecision) == 16384, "fft 192k");
        check(suggestedFftSize(192000, QualityMode::Efficient) == 8192, "fft 192k efficient");
    }

    // --- 5. Sine accuracy (Teste A) --------------------------------------
    {
        struct Case { double src, dst; };
        const Case cases[] = {{440, 444}, {432, 440}, {440, 442}, {442, 444}, {444, 432}};
        for (const auto& c : cases) {
            const double sr = 48000.0;
            const double f = c.dst / c.src;
            const std::size_t n = static_cast<std::size_t>(sr * 2.0); // 2 s
            std::vector<double> in(n);
            analysis::sine(in, c.src, sr, 0.5);
            PitchEngine tmp;
            EngineConfig cfg{sr, 1, f, QualityMode::HighPrecision};
            tmp.configure(cfg);
            const std::size_t skip = tmp.latencySamples() + static_cast<std::size_t>(sr * 0.25);
            auto out = renderMono(in, sr, f);
            const double got = analysis::peakFrequency(out.data(), out.size(), sr, skip);
            const double err = std::fabs(got - c.dst);
            std::snprintf(buf, sizeof(buf), "(%.0f->%.0f: got %.4f Hz, err %.4f Hz)",
                          c.src, c.dst, got, err);
            check(err < 0.5, "sine pitch", buf);
        }
    }

    // --- 6. Harmonics scale by the same ratio (Teste B) -------------------
    {
        const double sr = 48000.0, f = 444.0 / 440.0;
        const double hs[] = {440, 880, 1320, 1760, 2200, 2640};
        bool allOk = true;
        std::string detail;
        for (double h : hs) {
            std::vector<double> in(static_cast<std::size_t>(sr * 1.5));
            analysis::sine(in, h, sr, 0.5);
            auto out = renderMono(in, sr, f);
            PitchEngine tmp;
            EngineConfig cfg{sr, 1, f, QualityMode::HighPrecision};
            tmp.configure(cfg);
            const std::size_t skip = tmp.latencySamples() + static_cast<std::size_t>(sr * 0.2);
            const double got = analysis::peakFrequency(out.data(), out.size(), sr, skip);
            const double want = h * f;
            const double errCents = 1200.0 * std::log2(got / want);
            char b2[128];
            std::snprintf(b2, sizeof(b2), "[%.0f->%.2f err %.2fc] ", h, got, errCents);
            detail += b2;
            if (std::fabs(errCents) > 8.0) allOk = false; // <8 cents gate
        }
        check(allOk, "harmonics ratio", detail);
    }

    // --- 7. Passthrough factor=1 (unity) ----------------------------------
    {
        const double sr = 48000.0;
        std::vector<double> in(static_cast<std::size_t>(sr * 1.0));
        analysis::sine(in, 1000.0, sr, 0.5);
        auto out = renderMono(in, sr, 1.0);
        PitchEngine tmp;
        EngineConfig cfg{sr, 1, 1.0, QualityMode::HighPrecision};
        tmp.configure(cfg);
        const std::size_t skip = tmp.latencySamples();
        const double rIn = analysis::rms(in.data(), skip, in.size());
        const double rOut = analysis::rms(out.data(), skip, out.size());
        const double gdb = 20.0 * std::log10(rOut / (rIn + 1e-30));
        std::snprintf(buf, sizeof(buf), "(rms delta %.3f dB)", gdb);
        check(std::fabs(gdb) < 1.0, "passthrough unity", buf);
    }

    // --- 8. Transients (Teste D) -------------------------------------------
    {
        const double sr = 48000.0;
        const std::size_t n = static_cast<std::size_t>(sr * 1.0);
        std::vector<double> in(n, 0.0);
        analysis::impulse(in, n / 2);
        auto out = renderMono(in, sr, 444.0 / 440.0);
        PitchEngine tmp;
        EngineConfig cfg{sr, 1, 444.0 / 440.0, QualityMode::HighPrecision};
        tmp.configure(cfg);
        const std::size_t lat = tmp.latencySamples();
        bool finite = true;
        for (double v : out) if (!(v == v) || std::fabs(v) > 10.0) finite = false;
        // Peak of response should sit at impulse + exact latency N, up to a
        // small transient smear (measured ~-13 samples with shifting).
        std::size_t kMax = 0;
        double m = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            if (std::fabs(out[i]) > m) { m = std::fabs(out[i]); kMax = i; }
        const long long drift = static_cast<long long>(kMax) -
                                static_cast<long long>(n / 2 + lat);
        std::snprintf(buf, sizeof(buf), "(peak %.3f drift %lld smp, lat %zu)", m, drift, lat);
        check(finite && m > 1e-4 && std::llabs(drift) < 1500,
              "impulse response", buf);
    }

    // --- 9. Stereo coherence ------------------------------------------------
    {
        const double sr = 48000.0;
        std::vector<double> mono(static_cast<std::size_t>(sr * 1.0));
        analysis::harmonicStack(mono, 440.0, 8, sr, 0.3);
        auto out = runEngine({mono, mono}, sr, 444.0 / 440.0);
        double dmax = 0.0;
        for (std::size_t i = 0; i < mono.size(); ++i)
            dmax = std::max(dmax, std::fabs(out[0][i] - out[1][i]));
        std::snprintf(buf, sizeof(buf), "(max L-R diff %.3e)", dmax);
        check(dmax < 1e-9, "stereo identical-in identical-out", buf);
    }

    // --- 10. Duration preserved ----------------------------------------------
    {
        const double sr = 96000.0;
        std::vector<double> in(static_cast<std::size_t>(sr * 3.0));
        analysis::sine(in, 440.0, sr, 0.4);
        auto out = renderMono(in, sr, 415.0 / 440.0);
        std::snprintf(buf, sizeof(buf), "(in %zu out %zu)", in.size(), out.size());
        check(out.size() == in.size(), "duration preserved", buf);
    }

    // --- 11. High-rate + above-20kHz preservation (Teste C/E) -----------------
    {
        const double sr = 192000.0, f = 444.0 / 440.0;
        // 30 kHz content must survive an upward shift (maps to ~30.27 kHz).
        std::vector<double> in(static_cast<std::size_t>(sr * 1.0));
        analysis::sine(in, 30000.0, sr, 0.5);
        auto out = renderMono(in, sr, f);
        PitchEngine tmp;
        EngineConfig cfg{sr, 1, f, QualityMode::HighPrecision};
        tmp.configure(cfg);
        const std::size_t skip = tmp.latencySamples() + static_cast<std::size_t>(sr * 0.2);
        const double got = analysis::peakFrequency(out.data(), out.size(), sr, skip);
        const double want = 30000.0 * f;
        const double mIn = analysis::magnitudeNear(in.data(), in.size(), sr, 30000.0, 0);
        const double mOut = analysis::magnitudeNear(out.data(), out.size(), sr, want, skip);
        std::snprintf(buf, sizeof(buf), "(30k->%.1f got %.1f, mag ratio %.3f)", want, got,
                      mOut / (mIn + 1e-30));
        check(std::fabs(got - want) < 15.0 && mOut > 0.25 * mIn, "30kHz preserved @192k", buf);

        // Near-Nyquist guard: 90 kHz shifted up must NOT fold back into band.
        std::vector<double> in2(static_cast<std::size_t>(sr * 1.0));
        analysis::sine(in2, 90000.0, sr, 0.5);
        auto out2 = renderMono(in2, sr, f);
        const double spur = analysis::worstSpuriousDb(out2.data(), out2.size(), sr,
                                                      90000.0 * f, 400.0, skip);
        std::snprintf(buf, sizeof(buf), "(worst in-band spurious %.1f dB)", spur);
        check(spur < -20.0, "no foldback aliasing @192k", buf);
    }

    // --- 12. Round trip 440->444->440 (Teste F) --------------------------------
    {
        const double sr = 48000.0;
        std::vector<double> in(static_cast<std::size_t>(sr * 2.0));
        analysis::harmonicStack(in, 440.0, 6, sr, 0.3);
        auto mid = renderMono(in, sr, 444.0 / 440.0);
        auto back = renderMono(mid, sr, 440.0 / 444.0);
        PitchEngine tmp;
        EngineConfig cfg{sr, 1, 1.0, QualityMode::HighPrecision};
        tmp.configure(cfg);
        const std::size_t skip = 2 * (tmp.latencySamples() + static_cast<std::size_t>(sr * 0.2));
        const double got = analysis::peakFrequency(back.data(), back.size(), sr, skip);
        const double errCents = 1200.0 * std::log2(got / 440.0);
        std::snprintf(buf, sizeof(buf), "(roundtrip f0 %.3f Hz, err %.2f cents)", got, errCents);
        check(std::fabs(errCents) < 15.0, "roundtrip pitch", buf);
    }

    // --- 13. Gain protection ---------------------------------------------------
    {
        GainProtection gp(-1.0, 5.0, 48000.0);
        std::vector<double> x(1024, 1.5); // hot block
        const double* ci[1] = {x.data()};
        double* co[1] = {x.data()};
        gp.observe(ci, 1, 1024);
        gp.apply(co, 1, 1024);
        const double peak = analysis::peakAbs(x.data(), 900, 1024);
        const double ceil = GainProtection::dbToLinear(-1.0);
        std::snprintf(buf, sizeof(buf), "(settled peak %.4f vs ceil %.4f)", peak, ceil);
        check(std::fabs(peak - ceil) < 0.02, "gain ceiling", buf);
    }

    // --- 14. Engine B (peak-rate lock + nearest-phase anchor) ---------------
    {
        const double sr = 48000.0, f = 444.0 / 440.0;
        std::vector<double> in(static_cast<std::size_t>(sr * 2.0));
        analysis::sine(in, 440.0, sr, 0.5);
        auto out = renderMono(in, sr, f, 512, true);
        PitchEngine tmp;
        EngineConfig cfg{sr, 1, f, QualityMode::HighPrecision};
        tmp.configure(cfg);
        const std::size_t skip = tmp.latencySamples() + static_cast<std::size_t>(sr * 0.25);
        const double got = analysis::peakFrequency(out.data(), out.size(), sr, skip);
        std::snprintf(buf, sizeof(buf), "(got %.4f Hz)", got);
        check(std::fabs(got - 444.0) < 0.5, "engineB sine pitch", buf);
        const double spur = analysis::worstSpuriousDb(out.data(), out.size(), sr, 444.0,
                                                      25.0, skip);
        std::snprintf(buf, sizeof(buf), "(worst spurious %.1f dB, tol 25 Hz)", spur);
        check(spur < -35.0, "engineB sine spurious", buf);
        // Unlucky-alignment level check (660 Hz class): must not collapse.
        // NOTE: measure input and output over the SAME interior segment with
        // the same FFT size (different N would bias via scalloping).
        std::vector<double> in2(static_cast<std::size_t>(sr * 1.5));
        analysis::sine(in2, 660.0, sr, 0.5);
        auto out2 = renderMono(in2, sr, f, 512, true);
        const double mIn = analysis::magnitudeNear(in2.data(), in2.size(), sr, 660.0, skip);
        const double mOut = analysis::magnitudeNear(out2.data(), out2.size(), sr,
                                                    660.0 * f, skip);
        std::snprintf(buf, sizeof(buf), "(level ratio %.3f)", mOut / (mIn + 1e-30));
        check(mOut > 0.5 * mIn, "engineB 660Hz level", buf);
    }

    // --- 15. Engine A regression (phaseLock OFF still functional) ------------
    {
        const double sr = 48000.0, f = 444.0 / 440.0;
        std::vector<double> in(static_cast<std::size_t>(sr * 2.0));
        analysis::sine(in, 440.0, sr, 0.5);
        auto out = renderMono(in, sr, f, 512, false);
        PitchEngine tmp;
        EngineConfig cfg{sr, 1, f, QualityMode::HighPrecision};
        tmp.configure(cfg);
        const std::size_t skip = tmp.latencySamples() + static_cast<std::size_t>(sr * 0.25);
        const double got = analysis::peakFrequency(out.data(), out.size(), sr, skip);
        std::snprintf(buf, sizeof(buf), "(got %.4f Hz)", got);
        check(std::fabs(got - 444.0) < 0.5, "engineA sine pitch", buf);
    }

    std::printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
