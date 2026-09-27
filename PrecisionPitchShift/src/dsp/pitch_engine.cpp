// PrecisionPitchShift — streaming pitch-shift engine implementation.
#include "pitch_engine.h"

#include "alias_protection.h"
#include "fft.h"
#include "spectral_processor.h"

#include <algorithm>
#include <cmath>

namespace pps {

std::size_t suggestedFftSize(double sampleRate, QualityMode q) {
    std::size_t n;
    if (sampleRate <= 51000.0) n = 4096;
    else if (sampleRate <= 102000.0) n = 8192;
    else n = 16384;
    if (q == QualityMode::Efficient) n /= 2;
    if (n < 1024) n = 1024;
    return n;
}

PitchEngine::PitchEngine() = default;

namespace {
// Median of a small history (copy-based; histories are <= 8 entries).
double medianOf(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    std::vector<double> s = v;
    const std::size_t m = s.size() / 2;
    std::nth_element(s.begin(), s.begin() + m, s.end());
    double med = s[m];
    if (s.size() % 2 == 0) {
        std::nth_element(s.begin(), s.begin() + (m - 1), s.end());
        med = 0.5 * (med + s[m - 1]);
    }
    return med;
}
} // namespace

bool PitchEngine::configure(const EngineConfig& cfg) {
    if (cfg.sampleRate <= 0.0 || cfg.numChannels <= 0 || cfg.numChannels > 8)
        return false;
    if (!(cfg.factor > 0.0) || cfg.factor > 8.0 || cfg.factor < 0.125)
        return false;
    cfg_ = cfg;
    sampleRate_ = cfg.sampleRate;
    numChannels_ = cfg.numChannels;
    factor_ = cfg.factor;
    fftSize_ = cfg.fftSizeOverride >= 1024 && isPowerOfTwo(cfg.fftSizeOverride)
                   ? cfg.fftSizeOverride
                   : suggestedFftSize(sampleRate_, cfg.quality);
    hop_ = static_cast<int>(fftSize_ / 4);

    makeWindow(window_, fftSize_, cfg.window);
    computeAliasGuard(guard_, fftSize_, factor_);

    // WOLA normalisation: analysis+synthesis window, hop=N/4.
    // Gain = 1 / overlapAddSum, with overlapAddSum = (N/H)*mean(w^2)
    // (exactly constant across time for Hann; near-constant for BH).
    olaGain_ = 1.0 / ((static_cast<double>(fftSize_) / static_cast<double>(hop_)) *
                       meanSquare(window_));

    channels_.clear();
    channels_.resize(static_cast<std::size_t>(numChannels_));
    consumed_ = 0;
    drained_ = 0;
    const std::size_t nb = fftSize_ / 2 + 1;
    for (auto& ch : channels_) {
        ch.inFifo.clear();
        ch.outFifo.clear();
        ch.ola.assign(fftSize_, 0.0);
        ch.phase.resize(nb);
        ch.frame.assign(fftSize_, 0.0);
        ch.windowed.assign(fftSize_, 0.0);
        ch.synthFrame.assign(fftSize_, 0.0);
        ch.specA.assign(nb, {});
        ch.specS.assign(nb, {});
        ch.magA.assign(nb, 0.0);
        ch.phaA.assign(nb, 0.0);
        ch.trueF.assign(nb, 0.0);
        ch.magS.assign(nb, 0.0);
        ch.phaS.assign(nb, 0.0);
        ch.work.clear();
        ch.olaPos = 0;
    }
    return true;
}

void PitchEngine::reset() {
    consumed_ = 0;
    drained_ = 0;
    for (auto& ch : channels_) {
        ch.inFifo.clear();
        ch.outFifo.clear();        std::fill(ch.ola.begin(), ch.ola.end(), 0.0);
        ch.fluxHist.clear();
        const std::size_t nb = fftSize_ / 2 + 1;
        ch.phase.resize(nb);
        ch.olaPos = 0;
    }
}

void PitchEngine::setFactor(double factor) {
    if (!(factor > 0.0) || factor > 8.0 || factor < 0.125) return;
    factor_ = factor;
    cfg_.factor = factor;
    computeAliasGuard(guard_, fftSize_, factor_);
    for (auto& ch : channels_) {
        const std::size_t nb = fftSize_ / 2 + 1;
        ch.phase.resize(nb); // reset phase continuity on factor change
    }
}

bool PitchEngine::takeFrame(Channel& ch) {
    if (ch.inFifo.size() < fftSize_) return false;
    for (std::size_t i = 0; i < fftSize_; ++i) ch.frame[i] = ch.inFifo[i];
    ch.inFifo.erase(ch.inFifo.begin(), ch.inFifo.begin() + hop_);
    return true;
}

void PitchEngine::processFrame(Channel& ch, bool transient) {
    const std::size_t nb = fftSize_ / 2 + 1;
    for (std::size_t i = 0; i < fftSize_; ++i)
        ch.windowed[i] = ch.frame[i] * window_[i];

    rfft(ch.windowed.data(), ch.specA.data(), fftSize_, ch.work);
    for (std::size_t k = 0; k < nb; ++k) {
        ch.magA[k] = std::abs(ch.specA[k]);
        ch.phaA[k] = std::arg(ch.specA[k]);
    }

    if (!ch.phase.initialized) {
        for (std::size_t k = 0; k < nb; ++k) {
            const double omega = kTwoPi * static_cast<double>(k) /
                                 static_cast<double>(fftSize_);
            ch.trueF[k] = omega; // centre frequency until 2nd frame
        }
        // Keep initialized==false: propagateFrame() anchors frame 1.
    } else {
        estimateTrueFrequencies(ch.phaA.data(), ch.phase.prevAnalysis.data(),
                                ch.trueF.data(), nb, fftSize_, hop_);
    }

    const bool effTransient = transient && ch.phase.initialized;
    propagateFrame(ch.magA.data(), ch.phaA.data(), ch.trueF.data(),
                   ch.magS.data(), ch.phaS.data(), guard_.data(), nb, fftSize_,
                   factor_, hop_, effTransient, ch.phase, cfg_.phaseLock,
                   ch.scratch, ch.scratch2);
    ch.phase.initialized = true;

    // Rebuild complex spectrum. DC/Nyquist stay real-positive.
    for (std::size_t k = 0; k < nb; ++k) {
        if (k == 0 || k == nb - 1)
            ch.specS[k] = std::complex<double>(ch.magS[k], 0.0);
        else
            ch.specS[k] = std::polar(ch.magS[k], ch.phaS[k]);
    }
    rifft(ch.specS.data(), ch.synthFrame.data(), fftSize_, ch.work);

    for (std::size_t i = 0; i < fftSize_; ++i)
        ch.ola[i] += ch.synthFrame[i] * window_[i] * olaGain_;

    // Emit one hop of output.
    for (int i = 0; i < hop_; ++i)
        ch.outFifo.push_back(ch.ola[static_cast<std::size_t>(i)]);
    ch.ola.erase(ch.ola.begin(), ch.ola.begin() + hop_);
    ch.ola.resize(fftSize_, 0.0);

    ch.phase.prevAnalysis = ch.phaA;
    ch.phase.prevMag = ch.magA;
}

void PitchEngine::process(const double* const* in, double** out, int numSamples) {
    if (numSamples <= 0 || channels_.empty()) return;
    // 1) Append input, run whole frames. Transient decision shared across
    //    channels (max flux) to keep stereo coherent.
    for (int n = 0; n < numSamples; ++n)
        for (int c = 0; c < numChannels_; ++c)
            channels_[static_cast<std::size_t>(c)].inFifo.push_back(in[c][n]);
    consumed_ += numSamples;

    for (;;) {
        bool ready = true;
        for (auto& ch : channels_)
            if (ch.inFifo.size() < fftSize_) { ready = false; break; }
        if (!ready) break;

        double crestMax = 0.0;
        bool fluxTransient = false;
        for (auto& ch : channels_) {
            // Peek magnitudes of the candidate frame for flux estimation.
            for (std::size_t i = 0; i < fftSize_; ++i)
                ch.frame[i] = ch.inFifo[i];
            for (std::size_t i = 0; i < fftSize_; ++i)
                ch.windowed[i] = ch.frame[i] * window_[i];
            crestMax = std::max(crestMax, timeCrest(ch.windowed.data(), fftSize_));
            rfft(ch.windowed.data(), ch.specA.data(), fftSize_, ch.work);
            const std::size_t nb = fftSize_ / 2 + 1;
            for (std::size_t k = 0; k < nb; ++k) ch.magA[k] = std::abs(ch.specA[k]);
            if (ch.phase.initialized) {
                const double f = spectralFlux(ch.magA.data(), ch.phase.prevMag.data(), nb);
                // Adaptive attack gate: absolute floor AND spike above the
                // local BACKGROUND (median of history). History always grows
                // (firing frames contribute the background level, never the
                // spike), so it converges in a few frames even when the
                // background itself exceeds the floor (busy music): onsets
                // anchor 2-4 frames while sustain stays out.
                const double bg = medianOf(ch.fluxHist);
                const bool chanFire =
                    (f > cfg_.transientThreshold) &&
                    (ch.fluxHist.size() < 2 || f > cfg_.transientRatio * bg);
                if (chanFire) {
                    fluxTransient = true;
                    ch.fluxHist.push_back(std::max(bg, cfg_.transientThreshold));
                } else {
                    ch.fluxHist.push_back(f);
                }
                if (ch.fluxHist.size() > 8) ch.fluxHist.erase(ch.fluxHist.begin());
            }
        }
        const bool transient = fluxTransient || crestMax > cfg_.crestThreshold;
        for (auto& ch : channels_) {
            // Consume one hop; the spectrum was already computed above.
            for (std::size_t i = 0; i < fftSize_; ++i) ch.frame[i] = ch.inFifo[i];
            ch.inFifo.erase(ch.inFifo.begin(), ch.inFifo.begin() + hop_);
            // Recompute spectrum inside processFrame path: reuse by processing
            // the already-peeked spectrum. To avoid a second FFT we call the
            // tail of processFrame manually via cached specA:
            const std::size_t nb = fftSize_ / 2 + 1;
            for (std::size_t k = 0; k < nb; ++k) {
                ch.magA[k] = std::abs(ch.specA[k]);
                ch.phaA[k] = std::arg(ch.specA[k]);
            }
            if (!ch.phase.initialized) {
                for (std::size_t k = 0; k < nb; ++k)
                    ch.trueF[k] = kTwoPi * static_cast<double>(k) /
                                  static_cast<double>(fftSize_);
                // NOTE: keep initialized==false so propagateFrame() ANCHORS
                // synthesis phases to the analysis phases on the first frame.
            } else {
                estimateTrueFrequencies(ch.phaA.data(), ch.phase.prevAnalysis.data(),
                                        ch.trueF.data(), nb, fftSize_, hop_);
            }
            propagateFrame(ch.magA.data(), ch.phaA.data(), ch.trueF.data(),
                           ch.magS.data(), ch.phaS.data(), guard_.data(), nb,
                           fftSize_, factor_, hop_, transient, ch.phase,
                           cfg_.phaseLock, ch.scratch, ch.scratch2);
            ch.phase.initialized = true;
            for (std::size_t k = 0; k < nb; ++k) {
                if (k == 0 || k == nb - 1)
                    ch.specS[k] = std::complex<double>(ch.magS[k], 0.0);
                else
                    ch.specS[k] = std::polar(ch.magS[k], ch.phaS[k]);
            }
            rifft(ch.specS.data(), ch.synthFrame.data(), fftSize_, ch.work);
            for (std::size_t i = 0; i < fftSize_; ++i)
                ch.ola[i] += ch.synthFrame[i] * window_[i] * olaGain_;
            for (int i = 0; i < hop_; ++i)
                ch.outFifo.push_back(ch.ola[static_cast<std::size_t>(i)]);
            ch.ola.erase(ch.ola.begin(), ch.ola.begin() + hop_);
            ch.ola.resize(fftSize_, 0.0);
            ch.phase.prevAnalysis = ch.phaA;
            ch.phase.prevMag = ch.magA;
        }
    }

    // 2) Drain output FIFOs with an EXACT, block-size-independent latency of
    //    N samples: out[N + k*H + i] carries hop k (frame [kH, kH+N)), which
    //    exists once frame k completed, i.e. consumed >= k*H + N.
    //    Out[0:N] is startup silence. The host compensates via
    //    getLatencySamples() == N; the CLI pre-rolls N zeros and trims.
    //    Trailing tails are flushed by the host (tail samples) or by CLI
    //    post-roll zeros.
    const long long Nll = static_cast<long long>(fftSize_);
    const long long Hll = static_cast<long long>(hop_);
    for (int n = 0; n < numSamples; ++n) {
        bool release = false;
        if (drained_ >= Nll) {
            const long long k = (drained_ - Nll) / Hll; // hop index to emit
            release = consumed_ >= k * Hll + Nll;       // frame k completed
        }
        for (int c = 0; c < numChannels_; ++c) {
            auto& fifo = channels_[static_cast<std::size_t>(c)].outFifo;
            double s = 0.0;
            if (release && !fifo.empty()) {
                s = fifo.front();
                fifo.erase(fifo.begin());
            }
            out[c][n] = s;
        }
        ++drained_;
    }
}

} // namespace pps
