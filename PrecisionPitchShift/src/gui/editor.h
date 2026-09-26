// PrecisionPitchShift — custom editor view (see notes in this file).
#pragma once

#include "pluginterfaces/gui/iplugview.h"

namespace Steinberg::Vst {
class EditController;
}

namespace pps::gui {

// Returns a custom editor view, or nullptr when VSTGUI is not enabled — in
// which case the host (e.g. Audacity) shows its generic parameter UI.
Steinberg::IPlugView* createEditor(Steinberg::Vst::EditController* controller);

} // namespace pps::gui
