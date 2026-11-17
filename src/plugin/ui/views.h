#pragma once

#include "../meter_frame.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawdefs.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/cview.h"

#include <array>
#include <string>
#include <vector>

// Custom-drawn views for the editor. Everything is drawn in code from the theme; there are
// no bitmaps.

namespace fwyc::ui {

using VSTGUI::CColor;
using VSTGUI::CCoord;
using VSTGUI::CDrawContext;
using VSTGUI::CRect;

// Rounded, bordered card.
class Panel : public VSTGUI::CView
{
public:
    Panel (const CRect& size, CColor fill, CColor border, CCoord radius);
    void draw (CDrawContext* context) override;

private:
    CColor fill_;
    CColor border_;
    CCoord radius_;
};

// One line of text, vertically centred.
class Label : public VSTGUI::CView
{
public:
    Label (const CRect& size, std::string text, VSTGUI::SharedPointer<VSTGUI::CFontDesc> font, CColor color,
           VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText);

    void setText (const std::string& text);
    void setColor (CColor color);
    void draw (CDrawContext* context) override;

private:
    std::string text_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    CColor color_;
    VSTGUI::CHoriTxtAlign align_;
};

// Coloured dot and text, optionally inside a rounded outline ("Protected", "3 of 8 notches active").
class StatusPill : public VSTGUI::CView
{
public:
    StatusPill (const CRect& size, bool outlined, VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText);

    void setStatus (const std::string& text, CColor dot, CColor textColor);
    void draw (CDrawContext* context) override;

private:
    bool outlined_;
    VSTGUI::CHoriTxtAlign align_;
    std::string text_;
    CColor dot_;
    CColor textColor_;
};

// Outlined push button. A toggle shows its "on" state filled amber with onText.
class TextButton : public VSTGUI::CControl
{
public:
    TextButton (const CRect& size, VSTGUI::IControlListener* listener, int32_t tag, std::string text,
                std::string onText = {}, bool toggle = false, bool editsParameter = false);

    void draw (CDrawContext* context) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;

    CLASS_METHODS (TextButton, CControl)

private:
    std::string text_;
    std::string onText_;
    bool toggle_;
    bool editsParameter_;
    bool pressed_ {false};
};

// Row of mutually exclusive segments; value = index / (segments - 1).
class SegmentedControl : public VSTGUI::CControl
{
public:
    SegmentedControl (const CRect& size, VSTGUI::IControlListener* listener, int32_t tag,
                      std::vector<std::string> labels, CCoord fontSize, bool editsParameter);

    int selectedIndex () const;
    void draw (CDrawContext* context) override;
    void onMouseDownEvent (VSTGUI::MouseDownEvent& event) override;

    CLASS_METHODS (SegmentedControl, CControl)

private:
    CRect segmentRect (int index) const;

    std::vector<std::string> labels_;
    CCoord fontSize_;
    bool editsParameter_;
};

// 270 degree arc knob with a pointer. Drag vertically; Ctrl+click resets to the default.
class ArcKnob : public VSTGUI::CKnobBase
{
public:
    ArcKnob (const CRect& size, VSTGUI::IControlListener* listener, int32_t tag);

    void draw (CDrawContext* context) override;

    CLASS_METHODS (ArcKnob, CKnobBase)
};

// Vertical peak meter, -60..0 dBFS. A "warm" meter turns amber near the top.
class LevelMeter : public VSTGUI::CView
{
public:
    LevelMeter (const CRect& size, bool warm);

    void setLevelDb (float db);
    void draw (CDrawContext* context) override;

private:
    bool warm_;
    float levelDb_ {-120.0f};
};

// The "Detected" list: frequency, state, level at the mic and the cut applied.
class NotchList : public VSTGUI::CView
{
public:
    explicit NotchList (const CRect& size);

    void setNotches (const MeterFrame& frame, bool detectOnly);
    void draw (CDrawContext* context) override;

private:
    std::vector<MeterNotch> notches_;
    bool detectOnly_ {false};
};

// Input spectrum (live curve) or waterfall (20 s history), log frequency axis, with the
// unanalysed ranges hatched and engaged notches marked.
class Analyser : public VSTGUI::CView
{
public:
    enum class Mode
    {
        kSpectrum,
        kWaterfall,
    };

    explicit Analyser (const CRect& size);

    void setMode (Mode mode);
    void push (const MeterFrame& frame);
    void draw (CDrawContext* context) override;

private:
    static constexpr int kBands = MeterFrame::kSpectrumBands;
    static constexpr int kRows = 200;          // waterfall rows
    static constexpr int kFramesPerRow = 3;    // ~0.1 s per row at 30 frames/s
    static constexpr float kTopDb = 0.0f;      // spectrum scale
    static constexpr float kBottomDb = -90.0f; // the design's -72 hides a real mic's noise floor

    CRect plotRect () const;
    CCoord xForHz (const CRect& plot, double hz) const;
    void drawSpectrum (CDrawContext* context, const CRect& plot);
    void drawWaterfall (CDrawContext* context, const CRect& plot);
    void drawUnanalysed (CDrawContext* context, const CRect& plot, CCoord labelInset);
    void drawNotchMarkers (CDrawContext* context, const CRect& plot, bool lines);
    void drawFrequencyAxis (CDrawContext* context, const CRect& plot);
    void commitRow ();

    Mode mode_ {Mode::kSpectrum};
    std::array<float, kBands> displayDb_ {};
    float analysedMinHz_ {100.0f};
    float analysedMaxHz_ {12000.0f};
    std::vector<MeterNotch> notches_;

    // Waterfall: rows of colours, oldest first, plus the row being accumulated.
    std::vector<CColor> rows_;
    std::array<float, kBands> rowDb_ {};
    std::array<bool, kBands> rowFeedback_ {};
    int framesInRow_ {0};
    VSTGUI::SharedPointer<VSTGUI::CBitmap> bitmap_;
    bool bitmapDirty_ {true};
};

} // namespace fwyc::ui
