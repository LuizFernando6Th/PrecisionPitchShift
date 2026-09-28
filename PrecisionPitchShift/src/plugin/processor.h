// PrecisionPitchShift — VST3 audio processor (streaming DSP host side).
#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <vector>

#include "../dsp/gain_control.h"
#include "../dsp/pitch_engine.h"

namespace pps {

// Class IDs (stable — do not change after release).
static const Steinberg::FUID kProcessorUID(0xA14C7E21, 0x5D3B49F2, 0x8176C0AA,
                                           0x2E5D91B7);
static const Steinberg::FUID kControllerUID(0xC27F8A13, 0x6E41B5D9, 0x934A7C20,
                                             0xD18F56BE);

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() SMTG_OVERRIDE;

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE {
        return latencySamples_;
    }
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE {
        return latencySamples_; // allow the overlap-add tail to flush
    }

protected:
    void syncEngine(int numChannels);
    void syncEngineFromBuses();
    void readParameterChanges(Steinberg::Vst::IParameterChanges* changes);

    double sampleRate_ = 48000.0;
    double sourceHz_ = 440.0;
    double targetHz_ = 444.0;
    double lastReportedFactor_ = -1.0;
    int quality_ = 0; // 0 High Precision, 1 Efficient
    bool autoGain_ = false;
    double ceilingDb_ = -1.0;
    bool bypass_ = false;
    bool engineReady_ = false;
    Steinberg::uint32 latencySamples_ = 0;

    PitchEngine engine_;
    GainProtection gain_;
    // Scratch: float32 <-> float64 conversion + deinterleaved pointers.
    std::vector<double> tmpIn_[8];
    std::vector<double> tmpOut_[8];
    std::vector<const double*> inPtrs_;
    std::vector<double*> outPtrs_;
    std::vector<const double*> obsPtrs_; // for GainProtection::observe
    std::size_t maxBlockSize_ = 8192;
};

} // namespace pps
