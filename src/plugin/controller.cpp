// PrecisionPitchShift — VST3 controller implementation (v1.0.2).
#include "controller.h"

#include "parameters.h"
#include "../gui/editor.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace pps {

using namespace Steinberg;
using namespace Steinberg::Vst;

Controller::Controller() = default;

tresult PLUGIN_API Controller::initialize(FUnknown* context) {
    tresult r = EditController::initialize(context);
    if (r != kResultOk) return r;

    // NOTE: títulos e rótulos em PT-BR (o Audacity exibe os textos do plugin
    // como estão; não há tradução automática). Valores numéricos são
    // retornados SEM unidade (getParamStringByValue): o host anexa a unidade
    // sozinho — incluir unidade no texto duplica ("440,00 Hz Hz").
    parameters.addParameter(
        new RangeParameter(STR16("Frequencia de Origem"), params::kSourceFreq, STR16("Hz"),
                           params::kFreqMin,
                           params::kFreqMax,
                           params::kSourceDefault, 0,
                           ParameterInfo::kCanAutomate, kRootUnitId,
                           STR16("Origem")));

    parameters.addParameter(
        new RangeParameter(STR16("Frequencia de Destino"), params::kTargetFreq, STR16("Hz"),
                           params::kFreqMin,
                           params::kFreqMax,
                           params::kTargetDefault, 0,
                           ParameterInfo::kCanAutomate, kRootUnitId,
                           STR16("Destino")));

    RangeParameter* factorInfo = new RangeParameter(
        STR16("Fator de Tom"), params::kFactorInfo, STR16("x"), 0.1, 10.0,
        params::factor(params::kSourceDefault, params::kTargetDefault),
        0, ParameterInfo::kIsReadOnly, kRootUnitId);
    parameters.addParameter(factorInfo);

    auto* qualityParam = new StringListParameter(
        STR16("Processamento"), params::kQuality, nullptr,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsList, kRootUnitId,
        STR16("Qualidade"));
    {
        String128 s;
        UString(s, 128).fromAscii("Alta Precisao");
        qualityParam->appendString(s);
        UString(s, 128).fromAscii("Eficiente");
        qualityParam->appendString(s);
    }
    parameters.addParameter(qualityParam);

    // User-editable parameters are explicitly automatable so generic VST3
    // editors can expose them as controls.
    parameters.addParameter(
        new RangeParameter(STR16("Protecao de Ganho Automatica"), params::kAutoGain, nullptr,
                           0.0, 1.0, 0.0, 1, ParameterInfo::kCanAutomate,
                           kRootUnitId, STR16("Auto Gain")));

    parameters.addParameter(
        new RangeParameter(STR16("Teto"), params::kCeilingDb, STR16("dBFS"),
                           params::kCeilingMinDb,
                           params::kCeilingMaxDb,
                           params::kCeilingDefaultDb, 0,
                           ParameterInfo::kCanAutomate, kRootUnitId,
                           STR16("Ceiling")));

    parameters.addParameter(
        STR16("Bypass"), nullptr, 1, 0,
        ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass,
        params::kBypassId);

    // Expected public parameter table: source, target, factor, quality,
    // autogain, ceiling and bypass. Keep this deterministic so hosts cannot
    // encounter a partially registered parameter set.
    return parameters.getParameterCount() == 7 ? kResultOk : kResultFalse;
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
    int32 q[3] = {0, 0, 0}; // quality, autogain, bypass (see Processor state)
    if (state->read(v, sizeof(v), nullptr) != kResultOk) return kResultFalse;
    if (state->read(q, sizeof(q), nullptr) != kResultOk) return kResultFalse;
    setParamNormalized(params::kSourceFreq, params::freqToNorm(v[0]));
    setParamNormalized(params::kTargetFreq, params::freqToNorm(v[1]));
    setParamNormalized(params::kQuality, q[0] ? 1.0 : 0.0);
    setParamNormalized(params::kAutoGain, q[1] ? 1.0 : 0.0);
    setParamNormalized(params::kBypassId, q[2] ? 1.0 : 0.0);
    setParamNormalized(params::kCeilingDb, params::ceilingToNorm(v[4]));
    setParamNormalized(params::kFactorInfo,
                       params::factorToNorm(params::factor(v[0], v[1])));
    return kResultOk;
}

// Controller-only state (UI-agnostic version tag; all DSP state travels in
// the component state above). Returning kResultOk (instead of the base
// kNotImplemented) keeps host preset save/load working end to end.
tresult PLUGIN_API Controller::setState(IBStream* state) {
    if (!state) return kResultFalse;
    int32 version = 0;
    if (state->read(&version, sizeof(version), nullptr) != kResultOk)
        return kResultFalse;
    return kResultOk; // version reserved for future UI settings
}

tresult PLUGIN_API Controller::getState(IBStream* state) {
    if (!state) return kResultFalse;
    int32 version = 1;
    if (state->write(&version, sizeof(version), nullptr) != kResultOk)
        return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API Controller::getParamStringByValue(ParamID tag,
                                                     ParamValue valueNormalized,
                                                     String128 string) {
    char buf[64] = {0};
    switch (tag) {
        case params::kSourceFreq:
        case params::kTargetFreq:
            std::snprintf(buf, sizeof(buf), "%.2f", params::freqFromNorm(valueNormalized));
            break;
        case params::kFactorInfo:
            std::snprintf(buf, sizeof(buf), "%.9f",
                          params::factorFromNorm(valueNormalized));
            break;
        case params::kQuality:
            std::snprintf(buf, sizeof(buf), "%s",
                          valueNormalized > 0.5 ? "Eficiente" : "Alta Precisao");
            break;
        case params::kAutoGain:
            std::snprintf(buf, sizeof(buf), "%s", valueNormalized > 0.5 ? "Ligado" : "Desligado");
            break;
        case params::kCeilingDb:
            std::snprintf(buf, sizeof(buf), "%.1f",
                          params::ceilingFromNorm(valueNormalized));
            break;
        case params::kBypassId:
            std::snprintf(buf, sizeof(buf), "%s", valueNormalized > 0.5 ? "Ligado" : "Desligado");
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
    UString s(string, 128);
    char buf[64] = {0};
    s.toAscii(buf, sizeof(buf));
    // Aceita vírgula decimal PT-BR ("440,01") além do ponto.
    for (char* p = buf; *p; ++p)
        if (*p == ',') *p = '.';
    // Rótulos PT-BR (qualidade / chaves).
    if (tag == params::kQuality) {
        if (std::strstr(buf, "Efic") != nullptr) { valueNormalized = 1.0; return kResultTrue; }
        if (std::strstr(buf, "Alta") != nullptr) { valueNormalized = 0.0; return kResultTrue; }
        return kResultFalse;
    }
    if (tag == params::kAutoGain || tag == params::kBypassId) {
        if (std::strstr(buf, "Lig") != nullptr || std::strcmp(buf, "1") == 0) {
            valueNormalized = 1.0;
            return kResultTrue;
        }
        if (std::strstr(buf, "Des") != nullptr || std::strcmp(buf, "0") == 0) {
            valueNormalized = 0.0;
            return kResultTrue;
        }
        return kResultFalse;
    }
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
