// PrecisionPitchShift — VST3 processor implementation.
#include "processor.h"

#include "parameters.h"

#include <algorithm>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

namespace pps {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor() { setControllerClass(kControllerUID); }

Processor::~Processor() = default;

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    tresult r = AudioEffect::initialize(context);
    if (r != kResultOk) return r;
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API Processor::terminate() { return AudioEffect::terminate(); }

tresult PLUGIN_API Processor::setActive(TBool state) {
    if (state)
        syncEngineFromBuses(); // idempotent: no reset unless config changed
    else
        engineReady_ = false;
    return AudioEffect::setActive(state);
}

void Processor::syncEngineFromBuses() {
    Steinberg::Vst::SpeakerArrangement arr = Steinberg::Vst::SpeakerArr::kStereo;
    if (getBusArrangement(Steinberg::Vst::kInput, 0, arr) != Steinberg::kResultOk)
        arr = Steinberg::Vst::SpeakerArr::kStereo;
    int ch = Steinberg::Vst::SpeakerArr::getChannelCount(arr);
    if (ch < 1) ch = 1;
    if (ch > 8) ch = 8;
    syncEngine(ch);
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate > 0.0 ? setup.sampleRate : 48000.0;
    maxBlockSize_ = setup.maxSamplesPerBlock > 0
                        ? static_cast<std::size_t>(setup.maxSamplesPerBlock)
                        : 8192;
    gain_.setSampleRate(sampleRate_);
    syncEngineFromBuses(); // eager: latency valid before first process()
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    if (symbolicSampleSize == Vst::kSample32) return kResultTrue;
    if (symbolicSampleSize == Vst::kSample64) return kResultTrue;
    return kResultFalse;
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns == 1 && numOuts == 1 && inputs[0] == outputs[0]) {
        if (AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts) != kResultOk)
            return kResultFalse;
        engineReady_ = false;
        return kResultTrue;
    }
    return kResultFalse;
}

void Processor::syncEngine(int numChannels) {
    if (numChannels < 1) numChannels = 1;
    if (numChannels > 8) numChannels = 8;
    const double f = params::factor(sourceHz_, targetHz_);
    EngineConfig cfg;
    cfg.sampleRate = sampleRate_;
    cfg.numChannels = numChannels;
    cfg.factor = f;
    cfg.maxBlockSize = maxBlockSize_;
    cfg.quality = (quality_ == 1) ? QualityMode::Efficient : QualityMode::HighPrecision;
    const bool needNew = !engineReady_ || engine_.numChannels() != numChannels ||
                         engine_.sampleRate() != sampleRate_ ||
                         engine_.fftSize() != suggestedFftSize(sampleRate_, cfg.quality) ||
                         engine_.maxBlockSize() != maxBlockSize_;
    if (needNew) {
        const bool ok = engine_.configure(cfg);
        gain_.reset();
        engineReady_ = ok;
        if (!ok) {
            latencySamples_ = 0;
            return;
        }
        latencySamples_ = static_cast<uint32>(engine_.latencySamples());
    } else if (engine_.factor() != f) {
        engine_.setFactor(f);
    }
    // All temporary buffers and pointer arrays are prepared here, outside the
    // realtime processing loop.
    if (inPtrs_.size() != static_cast<std::size_t>(numChannels))
        inPtrs_.resize(static_cast<std::size_t>(numChannels));
    if (outPtrs_.size() != static_cast<std::size_t>(numChannels))
        outPtrs_.resize(static_cast<std::size_t>(numChannels));
    if (obsPtrs_.size() != static_cast<std::size_t>(numChannels))
        obsPtrs_.resize(static_cast<std::size_t>(numChannels));
    for (int c = 0; c < numChannels; ++c) {
        if (tmpIn_[c].size() < maxBlockSize_) tmpIn_[c].resize(maxBlockSize_);
        if (tmpOut_[c].size() < maxBlockSize_) tmpOut_[c].resize(maxBlockSize_);
    }
}

void Processor::readParameterChanges(IParameterChanges* changes) {
    if (!changes) return;
    const int32 count = changes->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        IParamValueQueue* q = changes->getParameterData(i);
        if (!q) continue;
        const ParamID id = q->getParameterId();
        int32 numPoints = q->getPointCount();
        if (numPoints <= 0) continue;
        int32 offset = 0;
        ParamValue value = 0.0;
        if (q->getPoint(numPoints - 1, offset, value) != kResultOk) continue;
        switch (id) {
            case params::kSourceFreq: sourceHz_ = params::freqFromNorm(value); break;
            case params::kTargetFreq: targetHz_ = params::freqFromNorm(value); break;
            case params::kQuality: quality_ = (value > 0.5) ? 1 : 0; break;
            case params::kAutoGain: autoGain_ = (value > 0.5); break;
            case params::kCeilingDb:
                ceilingDb_ = params::ceilingFromNorm(value);
                gain_.setCeilingDb(ceilingDb_);
                break;
            case params::kBypassId: bypass_ = (value > 0.5); break;
            default: break;
        }
    }
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    readParameterChanges(data.inputParameterChanges);

    if (data.numInputs == 0 || data.numOutputs == 0) return kResultOk;
    AudioBusBuffers& inBus = data.inputs[0];
    AudioBusBuffers& outBus = data.outputs[0];
    const int numCh = std::min<int>(inBus.numChannels, outBus.numChannels);
    if (numCh <= 0 || data.numSamples <= 0) return kResultOk;

    syncEngine(numCh);
    const int N = data.numSamples;
    if (!engineReady_ || N <= 0) {
        // Host contract normally guarantees N <= maxSamplesPerBlock. If a host
        // violates it, fail safe without allocating in the realtime callback.
        // A transparent copy is preferable to stale/undefined DSP output.
        if (data.symbolicSampleSize == Vst::kSample32) {
            for (int c = 0; c < numCh; ++c) {
                const float* in = inBus.channelBuffers32[c];
                float* out = outBus.channelBuffers32[c];
                for (int n = 0; n < N; ++n) out[n] = in[n];
            }
        } else {
            for (int c = 0; c < numCh; ++c) {
                const double* in = inBus.channelBuffers64[c];
                double* out = outBus.channelBuffers64[c];
                for (int n = 0; n < N; ++n) out[n] = in[n];
            }
        }
        outBus.silenceFlags = inBus.silenceFlags;
        return kResultOk;
    }
    if (static_cast<std::size_t>(N) > maxBlockSize_) {
        // Do not resize temporary buffers from the realtime thread. This is
        // a defensive fallback for a host violating setupProcessing().
        if (data.symbolicSampleSize == Vst::kSample32) {
            for (int c = 0; c < numCh; ++c) {
                const float* in = inBus.channelBuffers32[c];
                float* out = outBus.channelBuffers32[c];
                for (int n = 0; n < N; ++n) out[n] = in[n];
            }
        } else {
            for (int c = 0; c < numCh; ++c) {
                const double* in = inBus.channelBuffers64[c];
                double* out = outBus.channelBuffers64[c];
                for (int n = 0; n < N; ++n) out[n] = in[n];
            }
        }
        outBus.silenceFlags = inBus.silenceFlags;
        return kResultOk;
    }

    // Report derived factor to the (read-only) info parameter, best effort.
    const double f = params::factor(sourceHz_, targetHz_);
    if (data.outputParameterChanges && f != lastReportedFactor_) {
        int32 idx = 0;
        if (IParamValueQueue* q = data.outputParameterChanges->addParameterData(
                params::kFactorInfo, idx)) {
            q->addPoint(0, params::factorToNorm(f), idx);
            lastReportedFactor_ = f;
        }
    }

    if (bypass_) {
        // Hard bypass: copy input to output (both precisions).
        if (data.symbolicSampleSize == Vst::kSample32) {
            for (int c = 0; c < numCh; ++c) {
                const float* in = inBus.channelBuffers32[c];
                float* out = outBus.channelBuffers32[c];
                for (int n = 0; n < N; ++n) out[n] = in[n];
            }
        } else {
            for (int c = 0; c < numCh; ++c) {
                const double* in = inBus.channelBuffers64[c];
                double* out = outBus.channelBuffers64[c];
                for (int n = 0; n < N; ++n) out[n] = in[n];
            }
        }
        outBus.silenceFlags = inBus.silenceFlags;
        return kResultOk;
    }

    // Scratch is fully preallocated in setupProcessing()/syncEngine().
    // Never resize these vectors from the realtime callback.
    for (int c = 0; c < numCh; ++c) {
        inPtrs_[static_cast<size_t>(c)] = tmpIn_[c].data();
        outPtrs_[static_cast<size_t>(c)] = tmpOut_[c].data();
    }

    if (data.symbolicSampleSize == Vst::kSample32) {
        for (int c = 0; c < numCh; ++c) {
            const float* in = inBus.channelBuffers32[c];
            double* tmp = tmpIn_[c].data();
            for (int n = 0; n < N; ++n) tmp[n] = static_cast<double>(in[n]);
        }
        engine_.process(inPtrs_.data(), outPtrs_.data(), N);
        if (autoGain_) {
            for (int c = 0; c < numCh; ++c)
                obsPtrs_[static_cast<size_t>(c)] = outPtrs_[static_cast<size_t>(c)];
            gain_.observe(obsPtrs_.data(), numCh, N);
            gain_.apply(outPtrs_.data(), numCh, N);
        }
        for (int c = 0; c < numCh; ++c) {
            float* out = outBus.channelBuffers32[c];
            const double* tmp = tmpOut_[c].data();
            for (int n = 0; n < N; ++n) out[n] = static_cast<float>(tmp[n]);
        }
    } else {
        for (int c = 0; c < numCh; ++c) {
            const double* in = inBus.channelBuffers64[c];
            double* tmp = tmpIn_[c].data();
            for (int n = 0; n < N; ++n) tmp[n] = in[n];
        }
        engine_.process(inPtrs_.data(), outPtrs_.data(), N);
        if (autoGain_) {
            for (int c = 0; c < numCh; ++c)
                obsPtrs_[static_cast<size_t>(c)] = outPtrs_[static_cast<size_t>(c)];
            gain_.observe(obsPtrs_.data(), numCh, N);
            gain_.apply(outPtrs_.data(), numCh, N);
        }
        for (int c = 0; c < numCh; ++c) {
            double* out = outBus.channelBuffers64[c];
            const double* tmp = tmpOut_[c].data();
            for (int n = 0; n < N; ++n) out[n] = tmp[n];
        }
    }
    outBus.silenceFlags = 0;
    return kResultOk;
}

// --- State (program-agnostic, forward-compatible byte stream) ---
// Layout: v[5] doubles (source, target, reserved, reserved, ceiling),
//         q[3] int32 (quality, autogain, bypass). The controller parses the
// identical layout in setComponentState (required by the Steinberg
// BypassPersistence validator test: bypass must round-trip presets).
tresult PLUGIN_API Processor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    double v[5] = {sourceHz_, targetHz_, 0.0, 0.0, ceilingDb_};
    int32 q[3] = {quality_, autoGain_ ? 1 : 0, bypass_ ? 1 : 0};
    if (state->read(v, sizeof(v), nullptr) != kResultOk) return kResultFalse;
    if (state->read(q, sizeof(q), nullptr) != kResultOk) return kResultFalse;
    if (v[0] >= params::kFreqMin && v[0] <= params::kFreqMax) sourceHz_ = v[0];
    if (v[1] >= params::kFreqMin && v[1] <= params::kFreqMax) targetHz_ = v[1];
    if (q[0] == 0 || q[0] == 1) quality_ = q[0];
    autoGain_ = (q[1] != 0);
    bypass_ = (q[2] != 0);
    if (v[4] >= params::kCeilingMinDb && v[4] <= params::kCeilingMaxDb) {
        ceilingDb_ = v[4];
        gain_.setCeilingDb(ceilingDb_);
    }
    engineReady_ = false;
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    double v[5] = {sourceHz_, targetHz_, 0.0, 0.0, ceilingDb_};
    int32 q[3] = {quality_, autoGain_ ? 1 : 0, bypass_ ? 1 : 0};
    if (state->write(v, sizeof(v), nullptr) != kResultOk) return kResultFalse;
    if (state->write(q, sizeof(q), nullptr) != kResultOk) return kResultFalse;
    return kResultOk;
}

} // namespace pps
