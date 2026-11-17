#pragma once

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"

// Colours and type for the "Graphite" design (mockups/Feedback Detector Plugin UI.html).

namespace fwyc::ui::theme {

using VSTGUI::CColor;

inline const CColor kBackground {0x12, 0x13, 0x16, 0xff};
inline const CColor kPanel {0x1b, 0x1d, 0x22, 0xff};
inline const CColor kPanelBorder {0x26, 0x29, 0x30, 0xff};
inline const CColor kRow {0x23, 0x26, 0x2c, 0xff};
inline const CColor kBorder {0x2f, 0x33, 0x3b, 0xff};
inline const CColor kTrack {0x2a, 0x2d, 0x34, 0xff};
inline const CColor kWell {0x0c, 0x0d, 0x10, 0xff}; // waterfall background

inline const CColor kText {0xe8, 0xea, 0xee, 0xff};
inline const CColor kMuted {0x9a, 0xa0, 0xab, 0xff};
inline const CColor kDim {0x8a, 0x90, 0x9b, 0xff}; // axis labels
inline const CColor kIdle {0x4a, 0x4f, 0x5a, 0xff};

inline const CColor kAccent {0x4d, 0xd6, 0xc8, 0xff}; // teal: protection working
inline const CColor kAccentInk {0x06, 0x20, 0x1d, 0xff};
inline const CColor kWarn {0xff, 0xb5, 0x47, 0xff}; // amber: feedback
inline const CColor kWarnInk {0x1a, 0x13, 0x04, 0xff};
inline const CColor kHot {0xff, 0xe2, 0xb0, 0xff};
inline const CColor kDanger {0xff, 0x6b, 0x5e, 0xff}; // emergency cut engaged

inline CColor withAlpha (CColor c, double alpha)
{
    c.alpha = static_cast<uint8_t> (alpha * 255.0 + 0.5);
    return c;
}

inline CColor mix (const CColor& a, const CColor& b, double t)
{
    auto lerp = [t] (uint8_t x, uint8_t y) { return static_cast<uint8_t> (x + (y - x) * t + 0.5); };
    return {lerp (a.red, b.red), lerp (a.green, b.green), lerp (a.blue, b.blue), lerp (a.alpha, b.alpha)};
}

// The design uses Inter and JetBrains Mono; these are the closest faces every Windows machine has.
inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> uiFont (VSTGUI::CCoord size, bool semibold = false)
{
    return VSTGUI::makeOwned<VSTGUI::CFontDesc> (semibold ? "Segoe UI Semibold" : "Segoe UI", size);
}

inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> monoFont (VSTGUI::CCoord size)
{
    return VSTGUI::makeOwned<VSTGUI::CFontDesc> ("Consolas", size);
}

} // namespace fwyc::ui::theme
