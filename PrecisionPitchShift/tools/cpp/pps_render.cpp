// PrecisionPitchShift — offline render CLI (test/measurement front-end).
// Exercises the exact DSP core shipped in the VST3 (PitchEngine, double).
//
// Usage:
//   pps_render --in in.wav --out out.wav --source 440 --target 444
//              [--quality high|efficient] [--autogain on|off]
//              [--ceiling-db -1.0] [--block 512]
//
// No resampling is performed: out sample rate == in sample rate, always.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dsp/gain_control.h"
#include "dsp/pitch_engine.h"
#include "dsp/fft.h"
#include "dsp/spectral_processor.h"
#include "dsp/phase_processor.h"
#include "dsp/alias_protection.h"
#include "wav_io.h"

namespace {
struct Args {
    std::string in, out;
    double source = 440.0, target = 444.0;
    std::string quality = "high";
    bool autogain = true;
    double ceilingDb = -1.0;
    bool phaselock = true; // default ON (measured best); --phaselock off to compare
    int block = 512;
    int depth = 32;
    int fft = 0;
    std::string window = "hann";
    bool countTransients = false;
    // Diagnostic dump: --dump-bins k0,k1 --dump-frames N --dump-out file.csv
    // Writes per-frame analysis (mag + estimated true frequency) for bins
    // [k0..k1] of channel 0. No pitch shifting; for validating estimators.
    int dumpK0 = -1, dumpK1 = -1, dumpFrames = 0;
    std::string dumpOut;
};

bool parse(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        auto need = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        std::string v;
        if (k == "--in") { if (!need(a.in)) return false; }
        else if (k == "--out") { if (!need(a.out)) return false; }
        else if (k == "--source") { if (!need(v)) return false; a.source = std::stod(v); }
        else if (k == "--target") { if (!need(v)) return false; a.target = std::stod(v); }
        else if (k == "--quality") { if (!need(a.quality)) return false; }
        else if (k == "--autogain") {
            if (!need(v)) return false;
            a.autogain = (v == "on" || v == "1" || v == "true");
        }
        else if (k == "--ceiling-db") { if (!need(v)) return false; a.ceilingDb = std::stod(v); }
        else if (k == "--phaselock") {
            if (!need(v)) return false;
            a.phaselock = (v == "on" || v == "1" || v == "true");
        }
        else if (k == "--block") { if (!need(v)) return false; a.block = std::stoi(v); }
        else if (k == "--depth") { if (!need(v)) return false; a.depth = std::stoi(v); }
        else if (k == "--fft") { if (!need(v)) return false; a.fft = std::stoi(v); }
        else if (k == "--window") { if (!need(a.window)) return false; }
        else if (k == "--count-transients") { a.countTransients = true; }
        else if (k == "--dump-bins") {
            if (!need(v)) return false;
            const std::size_t c = v.find(',');
            if (c == std::string::npos) return false;
            a.dumpK0 = std::stoi(v.substr(0, c));
            a.dumpK1 = std::stoi(v.substr(c + 1));
        }
        else if (k == "--dump-frames") { if (!need(v)) return false; a.dumpFrames = std::stoi(v); }
        else if (k == "--dump-out") { if (!need(a.dumpOut)) return false; }
        else return false;
    }
    const bool dumpMode = a.dumpFrames > 0 && !a.dumpOut.empty() && a.dumpK0 >= 0;
    if (dumpMode) return !a.in.empty() && a.dumpK1 >= a.dumpK0;
    return !a.in.empty() && !a.out.empty() && a.source > 0.0 && a.target > 0.0 &&
           a.block > 0 && (a.depth == 16 || a.depth == 24 || a.depth == 32);
}
} // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parse(argc, argv, a)) {
        std::fprintf(stderr,
                     "usage: pps_render --in in.wav --out out.wav --source X --target Y "
                     "[--quality high|efficient] [--autogain on|off] [--ceiling-db DB] "
                     "[--block N] [--depth 16|24|32] [--fft N] [--window hann|bh] "
                     "[--phaselock on|off]\n"
                     "   or: pps_render --in in.wav --dump-bins k0,k1 --dump-frames N "
                     "--dump-out d.csv (analysis diagnostic)\n");
        return 2;
    }
    pps::wav::AudioFile af;
    std::string err;
    if (!pps::wav::read(a.in, af, err)) {
        std::fprintf(stderr, "read failed: %s\n", err.c_str());
        return 1;
    }
    if (a.dumpFrames > 0 && a.dumpK0 >= 0) {
        // --- Diagnostic: dump analysis magnitudes + true frequencies -------
        const std::size_t N =
            a.fft > 0 ? static_cast<std::size_t>(a.fft)
                      : pps::suggestedFftSize(af.sampleRate, pps::QualityMode::HighPrecision);
        const int H = static_cast<int>(N / 4);
        const std::size_t nb = N / 2 + 1;
        std::vector<double> win;
        pps::makeWindow(
            win, N,
            (a.window == "bh" || a.window == "blackmanharris")
                ? pps::WindowType::BlackmanHarris
                : pps::WindowType::Hann);
        pps::PhaseState st;
        st.resize(nb);
        std::vector<std::complex<double>> spec(nb), work;
        std::vector<double> frame(N), wframe(N), mag(nb), pha(nb), tru(nb),
                            magS(nb), phaS(nb), prevSyn(nb, 0.0), scratch, scratchB;
        pps::PhaseState pst;
        pst.resize(nb);
        // Source/target only affect factor for the synthesis replication.
        const double dumpFactor = 444.0 / 440.0;
        std::vector<double> dguard;
        pps::computeAliasGuard(dguard, N, dumpFactor);
        FILE* f = std::fopen(a.dumpOut.c_str(), "w");
        if (!f) { std::fprintf(stderr, "cannot open dump output\n"); return 1; }
        std::fprintf(f, "frame,bin,mag,trueHz,synHz,anaPha,synPha\n");
        const std::size_t n = af.channels[0].size();
        int dumped = 0;
        for (std::size_t off = 0; off + N <= n && dumped < a.dumpFrames; off += H, ++dumped) {
            for (std::size_t i = 0; i < N; ++i) frame[i] = af.channels[0][off + i];
            for (std::size_t i = 0; i < N; ++i) wframe[i] = frame[i] * win[i];
            pps::rfft(wframe.data(), spec.data(), N, work);
            for (std::size_t k = 0; k < nb; ++k) {
                mag[k] = std::abs(spec[k]);
                pha[k] = std::arg(spec[k]);
            }
            if (!st.initialized) {
                for (std::size_t k = 0; k < nb; ++k)
                    tru[k] = pps::kTwoPi * static_cast<double>(k) / static_cast<double>(N);
                st.initialized = true;
            } else {
                pps::estimateTrueFrequencies(pha.data(), st.prevAnalysis.data(), tru.data(),
                                             nb, N, H);
            }
            for (int k = a.dumpK0; k <= a.dumpK1 && k >= 0; ++k)
                std::fprintf(f, "%d,%d,%.6f,%.4f,%.4f,%.4f,%.4f\n", dumped, k, mag[k],
                             tru[k] * af.sampleRate / pps::kTwoPi, 0.0,
                             pha[static_cast<std::size_t>(k)], 0.0);
            // Replicate synthesis to record the actually applied advance rate.
            pps::propagateFrame(mag.data(), pha.data(), tru.data(), magS.data(),
                                phaS.data(), dguard.data(), nb, N, dumpFactor, H,
                                false, pst, true, scratch, scratchB);
            pst.initialized = true;
            if (dumped > 0) {
                for (int k = a.dumpK0; k <= a.dumpK1 && k >= 0; ++k) {
                    const double adv = (phaS[k] - prevSyn[k]) /
                                       static_cast<double>(H) * af.sampleRate /
                                       pps::kTwoPi;
                    std::fprintf(f, "%d,syn%d,%.6f,%.4f,%.4f,%.4f,%.4f\n", dumped, k, magS[k],
                                 0.0, adv, 0.0, phaS[k]);
                }
            }
            for (std::size_t k = 0; k < nb; ++k) prevSyn[k] = phaS[k];
            st.prevAnalysis = pha;
            st.prevMag = mag;
        }
        std::fclose(f);
        std::printf("dumped %d frames to %s\n", dumped, a.dumpOut.c_str());
        return 0;
    }
    pps::EngineConfig cfg;
    cfg.sampleRate = af.sampleRate;
    cfg.numChannels = af.numChannels;
    cfg.factor = a.target / a.source;
    cfg.quality = (a.quality == "efficient") ? pps::QualityMode::Efficient
                                            : pps::QualityMode::HighPrecision;
    cfg.phaseLock = a.phaselock;
    cfg.fftSizeOverride = a.fft > 0 ? static_cast<std::size_t>(a.fft) : 0;
    cfg.window = (a.window == "bh" || a.window == "blackmanharris")
                     ? pps::WindowType::BlackmanHarris
                     : pps::WindowType::Hann;
    pps::PitchEngine eng;
    if (!eng.configure(cfg)) {
        std::fprintf(stderr, "engine configure failed\n");
        return 1;
    }
    pps::GainProtection gp(a.ceilingDb, 5.0, af.sampleRate);

    // Offline correctness: pre-roll P zeros + post-roll P zeros, then trim
    // starting at L+P (engine lag L plus pre-roll). out_e[t] ~= in_e[t-L]
    // with in_e[t] = in[t-P], so out[t] = out_e[t+L+P] ~= in[t]: sample-exact
    // duration AND alignment. Startup zeros are dropped; the input tail is
    // fully rendered thanks to post-roll flushing the overlap-add.
    const std::size_t n = af.channels[0].size();
    const std::size_t P = eng.latencySamples();
    const std::size_t L = eng.latencySamples();
    std::vector<std::vector<double>> buf(af.channels.size());
    for (int c = 0; c < af.numChannels; ++c) {
        buf[c].assign(n + 2 * P, 0.0);
        for (std::size_t i = 0; i < n; ++i) buf[c][P + i] = af.channels[c][i];
    }
    std::vector<const double*> ip(static_cast<std::size_t>(af.numChannels));
    std::vector<double*> op(static_cast<std::size_t>(af.numChannels));
    const std::size_t nExt = n + 2 * P;
    for (std::size_t off = 0; off < nExt; off += static_cast<std::size_t>(a.block)) {
        const int m = static_cast<int>(std::min<std::size_t>(a.block, nExt - off));
        for (int c = 0; c < af.numChannels; ++c) {
            ip[static_cast<std::size_t>(c)] = buf[c].data() + off;
            op[static_cast<std::size_t>(c)] = buf[c].data() + off;
        }
        // In-place is safe: the engine buffers input before emitting output.
        // (Reads of in[c][..] all happen before any write to out[c][..].)
        eng.process(ip.data(),
                    const_cast<double**>(op.data()), // NOLINT: aliased, safe here
                    m);
        if (a.autogain) {
            // Apply latched protection causally (same as VST3 process()).
            std::vector<const double*> cp(op.size());
            for (std::size_t c = 0; c < op.size(); ++c) cp[c] = op[c];
            gp.observe(cp.data(), af.numChannels, m);
            gp.apply(op.data(), af.numChannels, m);
        }
    }
    // Trim pre-roll/lag/post-roll back into af (duration preserved exactly).
    const std::size_t T = L + P;
    for (int c = 0; c < af.numChannels; ++c)
        for (std::size_t i = 0; i < n; ++i) af.channels[c][i] = buf[c][T + i];
    if (!pps::wav::write(a.out, af, a.depth, err)) {
        std::fprintf(stderr, "write failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("rendered %.6f Hz -> %.6f Hz (factor %.9f) @ %.0f Hz, %d ch, %zu frames, "
                "latency %zu, peak %.4f, gain %.4f\n",
                a.source, a.target, cfg.factor, af.sampleRate, af.numChannels, n,
                eng.latencySamples(), gp.peakMax(), gp.currentGain());
    if (a.countTransients)
        std::fprintf(stderr,
                     "frames with anchored bins: %lld / %lld (%.1f%%); "
                     "anchored bins: %lld / %lld (%.2f%%)\n",
                     eng.dbgTransient(), eng.dbgFrames(),
                     100.0 * eng.dbgTransient() / (eng.dbgFrames() ? eng.dbgFrames() : 1),
                     eng.dbgAnchoredBins(), eng.dbgTotalBins(),
                     100.0 * eng.dbgAnchoredBins() / (eng.dbgTotalBins() ? eng.dbgTotalBins() : 1));
    return 0;
}
