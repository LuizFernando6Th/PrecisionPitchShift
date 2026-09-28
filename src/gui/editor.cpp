// PrecisionPitchShift — editor implementation.
// Default (PPS_HAS_VSTGUI undefined): return nullptr and let the host show
// its generic parameter UI (this is what Audacity uses — full functionality).
// The VSTGUI branch below is a STARTING POINT for a custom view, not a tested
// feature: enabling it additionally requires the vstgui submodule, a .uidesc
// layout and verification against the VSTGUI API of the SDK in use.
#include "editor.h"

#ifdef PPS_HAS_VSTGUI
// --- VSTGUI custom view -------------------------------------------
#include "vstgui/vstgui.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include "../plugin/parameters.h"

#include <cstdio>

namespace pps::gui {
namespace {

using namespace VSTGUI;

class PitchEditor : public VSTGUI::Vst3Editor {
public:
    explicit PitchEditor(Steinberg::Vst::EditController* c)
    : Vst3Editor(c, "view", "pitch_editor.uidesc") {}

    bool open(void* parent, const PlatformType& platformType) override {
        if (!Vst3Editor::open(parent, platformType)) return false;
        CRect r(0, 0, 460, 260);
        frame->setSize(r.getWidth(), r.getHeight());
        auto* bg = new CTextLabel(r, "Mudanca de Tom de Alta Precisao");
        frame->addView(bg);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Factor Y/X + host-rate info via parameters");
        auto* info = new CTextLabel(CRect(10, 200, 450, 250), buf);
        frame->addView(info);
        return true;
    }
};

} // namespace

Steinberg::IPlugView* createEditor(Steinberg::Vst::EditController* controller) {
    return new PitchEditor(controller);
}

} // namespace pps::gui

#else
// --- Fallback: host generic UI ------------------------------------
namespace pps::gui {

Steinberg::IPlugView* createEditor(Steinberg::Vst::EditController*) {
    return nullptr;
}

} // namespace pps::gui
#endif
