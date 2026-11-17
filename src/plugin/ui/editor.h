#pragma once

#include "public.sdk/source/vst/vstguieditor.h"

#include <array>

namespace VSTGUI {
class CVSTGUITimer;
}

namespace fwyc {
class Controller;
}

namespace fwyc::ui {

class Analyser;
class ArcKnob;
class Label;
class LevelMeter;
class NotchList;
class SegmentedControl;
class StatusPill;
class TextButton;

// 960 x 600 editor built in code (no .uidesc), following mockups/Feedback Detector Plugin UI.html.
// Parameters are polled from the controller and meter frames pulled from it on a ~30 Hz timer.
class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener
{
public:
    static constexpr int kWidth = 960;
    static constexpr int kHeight = 600;

    explicit Editor (Controller* controller);

    bool PLUGIN_API open (void* parent, const VSTGUI::PlatformType& platformType) SMTG_OVERRIDE;
    void PLUGIN_API close () SMTG_OVERRIDE;

    // Knobs drag vertically whatever the host's preference.
    VSTGUI_INT32 getKnobMode () const SMTG_OVERRIDE;

    void valueChanged (VSTGUI::CControl* control) SMTG_OVERRIDE;

private:
    struct KnobColumn
    {
        Steinberg::Vst::ParamID id;
        ArcKnob* knob;
        Label* value;
    };

    void build ();
    void onTimer ();
    void syncParameters ();
    void updateMeters ();

    Controller* controller_;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
    Steinberg::uint32 lastMeterCount_ {0};
    float inDb_ {-120.0f};
    float outDb_ {-120.0f};

    // Owned by the frame; valid while open.
    StatusPill* statusPill_ {nullptr};
    StatusPill* emergencyPill_ {nullptr};
    TextButton* bypassButton_ {nullptr};
    SegmentedControl* viewToggle_ {nullptr};
    StatusPill* notchCount_ {nullptr};
    Analyser* analyser_ {nullptr};
    NotchList* notchList_ {nullptr};
    std::array<KnobColumn, 4> knobs_ {};
    SegmentedControl* modeToggle_ {nullptr};
    LevelMeter* inMeter_ {nullptr};
    LevelMeter* outMeter_ {nullptr};
};

} // namespace fwyc::ui
