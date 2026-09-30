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

// (Transient attack handling is per-bin inside propagateFrame;
// no global flux gate or history is needed.)

bool PitchEngine::configure(const EngineConfig& cfg) {
    if (cfg.sampleRate <= 0.0 || cfg.numChannels <= 0 || cfg.numChannels > 8)
        return false;
    if (!(cfg.factor > 0.0) || cfg.factor > 10.0 || cfg.factor < 0.1)
        return false;
    if (cfg.maxBlockSize == 0) return false;
    cfg_ = cfg;
    sampleRate_ = cfg.sampleRate;
    numChannels_ = cfg.numChannels;
    factor_ = cfg.factor;
    maxBlockSize_ = cfg.maxBlockSize;
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
    // Phase model comes ONLY from the explicit configuration field
    // (default: shared rotation). No environment variables, no build switch.
    rotMode_ = cfg.sharedRotation;
    shared_.resize(nb); magRef_.assign(nb,0.0); trueRef_.assign(nb,0.0); outMagRef_.assign(nb,0.0);
    for (auto& ch : channels_) {
        const std::size_t fifoCapacity = fftSize_ + maxBlockSize_ + static_cast<std::size_t>(hop_) + 16;
        ch.inFifo.prepare(fifoCapacity);
        ch.outFifo.prepare(fifoCapacity);
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
        ch.scratch.assign(nb, 1.0);
        ch.scratch2.assign(nb, 0.0);
        ch.work.resize(fftSize_);
    }
    return true;
}

void PitchEngine::reset() {
    shared_.reset();
    consumed_ = 0;
    drained_ = 0;
    for (auto& ch : channels_) {
        ch.inFifo.clear();
        ch.outFifo.clear();
        std::fill(ch.ola.begin(), ch.ola.end(), 0.0);
        ch.phase.reset();
    }
}

void PitchEngine::setFactor(double factor) {
    if (!(factor > 0.0) || factor > 10.0 || factor < 0.1) return;
    factor_ = factor;
    cfg_.factor = factor;
    computeAliasGuard(guard_, fftSize_, factor_);
    // Keep accumulated synthesis phase continuous across parameter changes.
    // The spectral map changes at the next frame, but there is no gratuitous
    // phase reset/zeroing of the oscillator state.

}

void PitchEngine::processBlock(const double* const* in, double** out, int numSamples) {
    if (numSamples <= 0) return;

    for (int n = 0; n < numSamples; ++n) {
        for (int c = 0; c < numChannels_; ++c) {
            auto& ch = channels_[static_cast<std::size_t>(c)];
            (void)ch.inFifo.push(in[c][n]);
        }
    }
    consumed_ += numSamples;

    for (;;) {
        bool ready = true;
        for (auto& ch : channels_) {
            if (ch.inFifo.size() < fftSize_) { ready = false; break; }
        }
        if (!ready) break;

        double crestMax = 0.0;
        const std::size_t nb = fftSize_ / 2 + 1;
        for (auto& ch : channels_) {
            ch.inFifo.copyFrontTo(ch.frame.data(), fftSize_);
            for (std::size_t i = 0; i < fftSize_; ++i)
                ch.windowed[i] = ch.frame[i] * window_[i];
            crestMax = std::max(crestMax, timeCrest(ch.windowed.data(), fftSize_));
            rfft(ch.windowed.data(), ch.specA.data(), fftSize_, ch.work);
            for (std::size_t k = 0; k < nb; ++k)
                ch.magA[k] = std::abs(ch.specA[k]);
        }
        const bool crestFire = crestMax > cfg_.crestThreshold;

        if (rotMode_) {
            for (auto& ch : channels_) {
                ch.inFifo.popN(static_cast<std::size_t>(hop_));
                for (std::size_t k = 0; k < nb; ++k) {
                    ch.magA[k] = std::abs(ch.specA[k]);
                    ch.phaA[k] = std::arg(ch.specA[k]);
                }
                if (!ch.phase.initialized) {
                    for (std::size_t k = 0; k < nb; ++k)
                        ch.trueF[k] = kTwoPi * static_cast<double>(k) / static_cast<double>(fftSize_);
                } else {
                    estimateTrueFrequencies(ch.phaA.data(), ch.phase.prevAnalysis.data(),
                                            ch.trueF.data(), nb, fftSize_, hop_);
                }
                mapSpectrum(ch.magA.data(), ch.phaA.data(), guard_.data(), nb, fftSize_,
                            factor_, ch.magS.data(), ch.phaS.data(), ch.scratch, ch.scratch2);
            }
            for (std::size_t j = 0; j < nb; ++j) {
                double e = 0.0, bm = -1.0; std::size_t bc = 0, ci = 0;
                for (auto& ch : channels_) { const double m = ch.magA[j]; e += m * m; if (m > bm) { bm = m; bc = ci; } ++ci; }
                magRef_[j] = std::sqrt(e);
                trueRef_[j] = channels_[bc].trueF[j];
                double eo = 0.0;
                for (auto& ch : channels_) eo += ch.magS[j] * ch.magS[j];
                outMagRef_[j] = std::sqrt(eo);
            }
            updateSharedRotation(magRef_.data(), trueRef_.data(), outMagRef_.data(), nb,
                                 factor_, hop_, crestFire, cfg_.phaseLock, shared_);
            for (auto& ch : channels_) {
                for (std::size_t k = 0; k < nb; ++k) {
                    if (k == 0 || k == nb - 1)
                        ch.specS[k] = std::complex<double>(ch.magS[k], 0.0);
                    else
                        ch.specS[k] = std::polar(ch.magS[k], ch.phaS[k] + shared_.rot[k]);
                }
                rifft(ch.specS.data(), ch.synthFrame.data(), fftSize_, ch.work);
                for (std::size_t i = 0; i < fftSize_; ++i)
                    ch.ola[i] += ch.synthFrame[i] * window_[i] * olaGain_;
                for (int i = 0; i < hop_; ++i)
                    (void)ch.outFifo.push(ch.ola[static_cast<std::size_t>(i)]);
                std::move(ch.ola.begin() + hop_, ch.ola.end(), ch.ola.begin());
                std::fill(ch.ola.end() - hop_, ch.ola.end(), 0.0);
                ch.phase.prevAnalysis = ch.phaA;
                ch.phase.initialized = true;
                // Diagnostics (pps_render --count-transients), same per-channel
                // accounting as the legacy path below.
                dbgAnchoredBins_ += static_cast<long long>(shared_.anchoredBins);
                dbgTotalBins_ += static_cast<long long>(nb);
                dbgGuideSwitches_ += static_cast<long long>(shared_.guideSwitches);
                dbgGuideAssignments_ += static_cast<long long>(nb);
            }
            ++dbgFrames_;
            continue;
        }
        for (auto& ch : channels_) {
            ch.inFifo.popN(static_cast<std::size_t>(hop_));
            for (std::size_t k = 0; k < nb; ++k) {
                ch.magA[k] = std::abs(ch.specA[k]);
                ch.phaA[k] = std::arg(ch.specA[k]);
            }
            if (!ch.phase.initialized) {
                for (std::size_t k = 0; k < nb; ++k)
                    ch.trueF[k] = kTwoPi * static_cast<double>(k) /
                                  static_cast<double>(fftSize_);
            } else {
                estimateTrueFrequencies(ch.phaA.data(), ch.phase.prevAnalysis.data(),
                                        ch.trueF.data(), nb, fftSize_, hop_);
            }

            propagateFrame(ch.magA.data(), ch.phaA.data(), ch.trueF.data(),
                           ch.magS.data(), ch.phaS.data(), guard_.data(), nb,
                           fftSize_, factor_, hop_, crestFire, ch.phase,
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
                (void)ch.outFifo.push(ch.ola[static_cast<std::size_t>(i)]);
            std::move(ch.ola.begin() + hop_, ch.ola.end(), ch.ola.begin());
            std::fill(ch.ola.end() - hop_, ch.ola.end(), 0.0);

            ch.phase.prevAnalysis = ch.phaA;
            ch.phase.prevMag = ch.magA;
            dbgAnchoredBins_ += static_cast<long long>(ch.phase.anchoredBins);
            dbgTotalBins_ += static_cast<long long>(nb);
            dbgGuideSwitches_ += static_cast<long long>(ch.phase.guideSwitches);
            dbgGuideAssignments_ += static_cast<long long>(nb);
        }
        ++dbgFrames_;
    }

    const long long Nll = static_cast<long long>(fftSize_);
    const long long Hll = static_cast<long long>(hop_);
    for (int n = 0; n < numSamples; ++n) {
        bool release = false;
        if (drained_ >= Nll) {
            const long long k = (drained_ - Nll) / Hll;
            release = consumed_ >= k * Hll + Nll;
        }
        for (int c = 0; c < numChannels_; ++c) {
            auto& fifo = channels_[static_cast<std::size_t>(c)].outFifo;
            double s = 0.0;
            if (release) (void)fifo.pop(s);
            out[c][n] = s;
        }
        ++drained_;
    }
}

void PitchEngine::process(const double* const* in, double** out, int numSamples) {
    if (!in || !out || numSamples <= 0 || channels_.empty()) return;

    int offset = 0;
    while (offset < numSamples) {
        const int chunk = static_cast<int>(std::min<std::size_t>(
            maxBlockSize_, static_cast<std::size_t>(numSamples - offset)));
        const double* subIn[8] = {};
        double* subOut[8] = {};
        for (int c = 0; c < numChannels_; ++c) {
            subIn[c] = in[c] + offset;
            subOut[c] = out[c] + offset;
        }
        processBlock(subIn, subOut, chunk);
        offset += chunk;
    }
}

} // namespace pps
