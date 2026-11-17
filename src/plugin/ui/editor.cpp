#include "editor.h"

#include "theme.h"
#include "views.h"

#include "../controller.h"
#include "../params.h"
#include "../plugin_ids.h"

#include "public.sdk/source/vst/utility/stringconvert.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cvstguitimer.h"

#include <algorithm>
#include <cstdio>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace VSTGUI;

namespace fwyc::ui {

namespace {

// Tags for controls that aren't parameters. Parameter controls use their ParamID.
constexpr int32_t kTagViewToggle = 1000;
constexpr int32_t kTagClearNotches = 1001;

constexpr uint32_t kTimerMs = 33;
constexpr float kMeterFallDbPerTick = 1.2f; // ~36 dB/s

// App mark: a pulse in a rounded square.
class Logo : public CView
{
public:
    explicit Logo (const CRect& size) : CView (size) { setMouseEnabled (false); }

    void draw (CDrawContext* context) override
    {
        const CRect r = getViewSize ();
        if (auto box = VSTGUI::owned (context->createRoundRectGraphicsPath (CRect (r).inset (1.5, 1.5), 6.5)))
        {
            context->setFillColor (theme::kPanel);
            context->drawGraphicsPath (box, CDrawContext::kPathFilled);
            context->setFrameColor (theme::kBorder);
            context->setLineWidth (1.0);
            context->drawGraphicsPath (box, CDrawContext::kPathStroked);
        }
        if (auto pulse = VSTGUI::owned (context->createGraphicsPath ()))
        {
            const CPoint points[] = {{5, 15}, {8, 15}, {10, 8}, {13, 19}, {15, 11}, {17, 15}, {21, 15}};
            pulse->beginSubpath (CPoint (r.left + points[0].x, r.top + points[0].y));
            for (size_t i = 1; i < std::size (points); ++i)
                pulse->addLine (CPoint (r.left + points[i].x, r.top + points[i].y));
            context->setFrameColor (theme::kAccent);
            context->setLineWidth (1.8);
            context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
            context->drawGraphicsPath (pulse, CDrawContext::kPathStroked);
        }
        setDirty (false);
    }
};

std::string paramText (Controller& controller, ParamID id)
{
    String128 text {};
    controller.getParamStringByValue (id, controller.getParamNormalized (id), text);
    return StringConvert::convert (text);
}

} // namespace

Editor::Editor (Controller* controller)
: VSTGUIEditor (controller), controller_ (controller)
{
    ViewRect size (0, 0, kWidth, kHeight);
    setRect (size);
}

VSTGUI_INT32 Editor::getKnobMode () const
{
    return VSTGUI::kLinearMode;
}

bool PLUGIN_API Editor::open (void* parent, const PlatformType& platformType)
{
    if (frame)
        return false;

    frame = new CFrame (CRect (0, 0, kWidth, kHeight), this);
    frame->setBackgroundColor (theme::kBackground);
    build ();
    frame->open (parent, platformType);

    lastMeterCount_ = controller_->meterFrameCount ();
    syncParameters ();
    timer_ = makeOwned<CVSTGUITimer> ([this] (CVSTGUITimer*) { onTimer (); }, kTimerMs, true);
    return true;
}

void PLUGIN_API Editor::close ()
{
    if (timer_)
    {
        timer_->stop ();
        timer_ = nullptr;
    }
    if (frame)
    {
        frame->forget ();
        frame = nullptr;
    }
    statusPill_ = emergencyPill_ = notchCount_ = nullptr;
    bypassButton_ = nullptr;
    viewToggle_ = modeToggle_ = nullptr;
    analyser_ = nullptr;
    notchList_ = nullptr;
    knobs_ = {};
    inMeter_ = outMeter_ = nullptr;
}

void Editor::build ()
{
    // --- Header: mark, name, status, bypass -----------------------------------------------
    frame->addView (new Logo (CRect (24, 15, 50, 41)));
    frame->addView (new Label (CRect (62, 13, 300, 29), "Whence", theme::uiFont (13, true), theme::kText));
    frame->addView (new Label (CRect (62, 28, 300, 43), "Feedback detector", theme::uiFont (11), theme::kMuted));

    statusPill_ = new StatusPill (CRect (548, 12, 664, 44), true);
    frame->addView (statusPill_);
    emergencyPill_ = new StatusPill (CRect (672, 12, 848, 44), true);
    frame->addView (emergencyPill_);
    bypassButton_ = new TextButton (CRect (856, 6, 936, 50), this, kBypassId, "Bypass", "Bypassed", true, true);
    frame->addView (bypassButton_);

    frame->addView (new Panel (CRect (0, 55, kWidth, 56), theme::kPanelBorder, theme::kPanelBorder, 0));

    // --- Input: spectrum / waterfall ------------------------------------------------------
    frame->addView (new Panel (CRect (24, 72, 684, 436), theme::kPanel, theme::kPanelBorder, 12));
    frame->addView (new Label (CRect (40, 88, 96, 128), "INPUT", theme::uiFont (12, true), theme::kMuted));
    viewToggle_ = new SegmentedControl (CRect (96, 88, 286, 128), this, kTagViewToggle, {"Spectrum", "Waterfall"}, 12,
                                        false);
    frame->addView (viewToggle_);
    notchCount_ = new StatusPill (CRect (430, 88, 668, 128), false, kRightText);
    frame->addView (notchCount_);
    analyser_ = new Analyser (CRect (40, 136, 668, 424));
    frame->addView (analyser_);

    // --- Detected notches -----------------------------------------------------------------
    frame->addView (new Panel (CRect (700, 72, 936, 436), theme::kPanel, theme::kPanelBorder, 12));
    frame->addView (new Label (CRect (716, 88, 840, 104), "DETECTED", theme::uiFont (12, true), theme::kMuted));
    frame->addView (new Label (CRect (800, 88, 920, 104), "level / cut", theme::uiFont (10), theme::kMuted, kRightText));
    notchList_ = new NotchList (CRect (716, 116, 920, 368));
    frame->addView (notchList_);
    frame->addView (new TextButton (CRect (716, 376, 920, 420), this, kTagClearNotches, "Clear all notches"));

    // --- Controls strip: knobs, mode, meters ----------------------------------------------
    frame->addView (new Panel (CRect (24, 452, 936, 580), theme::kPanel, theme::kPanelBorder, 12));

    const std::pair<ParamID, const char*> knobDefs[] = {
        {kSensitivityId, "SENSITIVITY"}, {kDepthId, "DEPTH"}, {kWidthId, "WIDTH"}, {kReleaseId, "RELEASE"}};
    for (size_t i = 0; i < knobs_.size (); ++i)
    {
        const CCoord x = 48 + static_cast<CCoord> (i) * 96;
        const ParamID id = knobDefs[i].first;
        auto* knob = new ArcKnob (CRect (x + 12, 464, x + 76, 528), this, static_cast<int32_t> (id));
        if (auto* param = controller_->getParameterObject (id))
            knob->setDefaultValue (static_cast<float> (param->getInfo ().defaultNormalizedValue));
        frame->addView (knob);
        frame->addView (new Label (CRect (x, 532, x + 88, 548), knobDefs[i].second, theme::uiFont (11), theme::kMuted,
                                   kCenterText));
        auto* value = new Label (CRect (x, 550, x + 88, 568), "", theme::monoFont (12), theme::kText, kCenterText);
        frame->addView (value);
        knobs_[i] = {id, knob, value};
    }

    frame->addView (new Label (CRect (526, 474, 750, 490), "MODE", theme::uiFont (11), theme::kMuted));
    modeToggle_ = new SegmentedControl (CRect (526, 494, 750, 538), this, kModeId, {"Auto notch", "Detect only"}, 13, true);
    frame->addView (modeToggle_);

    inMeter_ = new LevelMeter (CRect (865, 474, 875, 544), true);
    frame->addView (inMeter_);
    frame->addView (new Label (CRect (850, 548, 890, 562), "IN", theme::uiFont (10), theme::kMuted, kCenterText));
    outMeter_ = new LevelMeter (CRect (895, 474, 905, 544), false);
    frame->addView (outMeter_);
    frame->addView (new Label (CRect (880, 548, 920, 562), "OUT", theme::uiFont (10), theme::kMuted, kCenterText));
}

void Editor::valueChanged (CControl* control)
{
    const int32_t tag = control->getTag ();
    const ParamValue value = control->getValueNormalized ();
    switch (tag)
    {
        case kTagViewToggle:
            analyser_->setMode (value >= 0.5 ? Analyser::Mode::kWaterfall : Analyser::Mode::kSpectrum);
            return;
        case kTagClearNotches:
            if (value >= 0.5)
                controller_->clearNotches ();
            return;
        default:
            // A parameter: begin/end of the gesture are sent by the control through the frame.
            controller_->setParamNormalized (static_cast<ParamID> (tag), value);
            controller_->performEdit (static_cast<ParamID> (tag), value);
            syncParameters ();
            return;
    }
}

void Editor::onTimer ()
{
    syncParameters ();
    updateMeters ();
}

void Editor::syncParameters ()
{
    // Host automation and preset loads change parameters behind the editor's back.
    for (const auto& k : knobs_)
    {
        if (!k.knob)
            continue;
        const auto value = static_cast<float> (controller_->getParamNormalized (k.id));
        if (!k.knob->isEditing () && value != k.knob->getValueNormalized ())
        {
            k.knob->setValueNormalized (value);
            k.knob->invalid ();
        }
        k.value->setText (paramText (*controller_, k.id));
    }

    auto syncControl = [this] (CControl* control, ParamID id) {
        const auto value = static_cast<float> (controller_->getParamNormalized (id));
        if (value != control->getValueNormalized ())
        {
            control->setValueNormalized (value);
            control->invalid ();
        }
    };
    syncControl (bypassButton_, kBypassId);
    syncControl (modeToggle_, kModeId);

    const bool bypassed = controller_->getParamNormalized (kBypassId) >= 0.5;
    const bool detectOnly = params::detectOnly (controller_->getParamNormalized (kModeId));
    if (bypassed)
        statusPill_->setStatus ("Bypassed", theme::kIdle, theme::kMuted);
    else if (detectOnly)
        statusPill_->setStatus ("Detect only", theme::kWarn, theme::kText);
    else
        statusPill_->setStatus ("Protected", theme::kAccent, theme::kText);
}

void Editor::updateMeters ()
{
    const MeterFrame& meter = controller_->meterFrame ();
    const bool fresh = controller_->meterFrameCount () != lastMeterCount_;
    lastMeterCount_ = controller_->meterFrameCount ();

    // Peak meters fall smoothly between frames (and to silence when frames stop).
    inDb_ = std::max (fresh ? meter.inPeakDb : -120.0f, inDb_ - kMeterFallDbPerTick);
    outDb_ = std::max (fresh ? meter.outPeakDb : -120.0f, outDb_ - kMeterFallDbPerTick);
    inMeter_->setLevelDb (inDb_);
    outMeter_->setLevelDb (outDb_);

    if (!fresh)
        return;

    const bool detectOnly = params::detectOnly (controller_->getParamNormalized (kModeId));
    analyser_->push (meter);
    notchList_->setNotches (meter, detectOnly);

    char text[64];
    if (meter.notchesEngaged > 0)
    {
        std::snprintf (text, sizeof (text), "%d of %d notches active", meter.notchesEngaged, meter.notchCapacity);
        notchCount_->setStatus (text, theme::kWarn, theme::kWarn);
    }
    else
    {
        notchCount_->setStatus ("No notches active", theme::kIdle, theme::kMuted);
    }

    if (meter.emergencyReductionDb > 0.05f && !detectOnly)
    {
        std::snprintf (text, sizeof (text), "Emergency cut: -%.1f dB", meter.emergencyReductionDb);
        emergencyPill_->setStatus (text, theme::kDanger, theme::kText);
    }
    else
    {
        emergencyPill_->setStatus ("Emergency cut: idle", theme::kIdle, theme::kMuted);
    }
}

} // namespace fwyc::ui
