#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace fwyc {

// Class IDs are part of the plug-in's identity: hosts store them in projects. Never change them.
static const Steinberg::FUID kProcessorUID (0x2595013B, 0x2659ED4A, 0x98B69B49, 0xA09978AC);
static const Steinberg::FUID kControllerUID (0xF0FDC1CB, 0x1F4C2E49, 0x97F14E05, 0xBEC0411E);

enum ParamIds : Steinberg::Vst::ParamID
{
    kBypassId = 0,
};

// Bump when the processor state layout changes; setState() must keep reading older versions.
constexpr Steinberg::int32 kStateVersion = 1;

} // namespace fwyc
