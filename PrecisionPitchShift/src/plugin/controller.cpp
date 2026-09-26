// PrecisionPitchShift — VST3 controller implementation.
#include "controller.h"

#include "parameters.h"
#include "../gui/editor.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cstdio>
#include <string>

namespace pps {

using namespace Steinberg;
using namespace Steinberg::Vst;

Controller::Controller() = default;

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    tresult r = EditController::initialize(context);
    if (r != kResultOk) return r;

    parameters.addParameter(
        new RangeParameter(STR16("Source Frequency"), params::kSourceFreq, STR16("Hz"),
                           params::freqToNorm(params::kFreqMin),
                           params::freqToNorm(params::kFreqMax),
                           params::freqToNorm(params::kSourceDefault), 0,
                           ParameterInfo::kCanAutomate, kRootUnitId));

    parameters.addParameter(
        new RangeParameter(STR16("Target Frequency"), params::kTargetFreq, STR16("Hz"),
                           params::freqToNorm(params::kFreqMin),
                           params::freqToNorm(params::kFreqMax),
                           params::freqToNorm(params::kTargetDefault), 0,
                           ParameterInfo::kCanAutomate, kRootUnitId));

    RangeParameter* factorInfo = new RangeParameter(
        STR16("Pitch Factor"), params::kFactorInfo, STR16("x"), 0.0, 1.0,
        params::factorToNorm(params::factor(params::kSourceDefault,
                                            params::kTargetDefault)),
        0, ParameterInfo::kIsReadOnly, kRootUnitId);
    parameters.addParameter(factorInfo);

    auto* qualityParam = new StringListParameter(
        STR16("Processing"), params::kQuality, nullptr,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList, kRootUnitId);
    {
        String128 s;
        UString(s, 128).fromAscii("High Precision");
        qualityParam->appendString(s);
        UString(s, 128).fromAscii("Efficient");
        qualityParam->appendString(s);
    }
    parameters.addParameter(qualityParam);

    parameters.addParameter(
        new RangeParameter(STR16("Auto Gain Protection"), params::kAutoGain, nullptr,
                           0.0, 1.0, 1.0, 0, ParameterInfo::kCanAutomate,
                           kRootUnitId));

    parameters.addParameter(
        new RangeParameter(STR16("Ceiling"), params::kCeilingDb, STR16("dBFS"),
                           params::ceilingToNorm(params::kCeilingMinDb),
                           params::ceilingToNorm(params::kCeilingMaxDb),
                           params::ceilingToNorm(params::kCeilingDefaultDb), 0,
                           ParameterInfo::kCanAutomate, kRootUnitId));

    parameters.addParameter(
        STR16("Bypass"), nullptr, 1, 0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
        params::kBypassId);

    return kResultOk;
}

tresult PLUGIN_API Controller::terminate() { return EditController::terminate(); }

IPlugView* PLUGIN_API Controller::createView(FIDString name) {
    if (name && std::string(name) == ViewType::kEditor) {
        if (IPlugView* v = gui::createEditor(this)) return v;
    }
    return nullptr; // host falls back to its generic parameter UI (Audacity)
}

tresult PLUGIN_API Controller::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;
    double v[5] = {0, 0, 0, 0, 0};
    int32 q[2] = {0, 0};
    if (state->read(v, sizeof(v), nullptr) != kResultOk) return kResultFalse;
    if (state->read(q, sizeof(q), nullptr) != kResultOk) return kResultFalse;
    setParamNormalized(params::kSourceFreq, params::freqToNorm(v[0]));
    setParamNormalized(params::kTargetFreq, params::freqToNorm(v[1]));
    setParamNormalized(params::kQuality, q[0] ? 1.0 : 0.0);
    setParamNormalized(params::kAutoGain, q[1] ? 1.0 : 0.0);
    setParamNormalized(params::kCeilingDb, params::ceilingToNorm(v[4]));
    setParamNormalized(params::kFactorInfo,
                       params::factorToNorm(params::factor(v[0], v[1])));
    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag,
                                                     ParamValue valueNormalized,
                                                     String128 string) {
    char buf[64] = {0};
    switch (tag) {
        case params::kSourceFreq:
        case params::kTargetFreq:
            std::snprintf(buf, sizeof(buf), "%.2f Hz", params::freqFromNorm(valueNormalized));
            break;
        case params::kFactorInfo:
            std::snprintf(buf, sizeof(buf), "%.9f",
                          params::factorFromNorm(valueNormalized));
            break;
        case params::kQuality:
            std::snprintf(buf, sizeof(buf), "%s",
                          valueNormalized > 0.5 ? "Efficient" : "High Precision");
            break;
        case params::kAutoGain:
            std::snprintf(buf, sizeof(buf), "%s", valueNormalized > 0.5 ? "On" : "Off");
            break;
        case params::kCeilingDb:
            std::snprintf(buf, sizeof(buf), "%.1f dBFS",
                          params::ceilingFromNorm(valueNormalized));
            break;
        default:
            return EditController::getParamStringByValue(tag, valueNormalized, string);
    }
    UString s(string, 128);
    s.fromAscii(buf);
    return kResultTrue;
}

tresult PLUGIN_API Controller::getParamValueByString(ParamID tag, TChar* string,
                                                     ParamValue& valueNormalized) {
    if (!string) return kResultFalse;
    UString128 s(string, 128);
    char buf[64] = {0};
    s.toAscii(buf, sizeof(buf));
    double v = 0.0;
    if (std::sscanf(buf, "%lf", &v) != 1) return kResultFalse;
    switch (tag) {
        case params::kSourceFreq:
        case params::kTargetFreq: valueNormalized = params::freqToNorm(v); return kResultTrue;
        case params::kCeilingDb: valueNormalized = params::ceilingToNorm(v); return kResultTrue;
        default: break;
    }
    return kResultFalse;
}

} // namespace pps
