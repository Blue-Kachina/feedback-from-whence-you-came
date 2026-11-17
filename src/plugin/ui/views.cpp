#include "views.h"

#include "theme.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgraphicstransform.h"
#include "vstgui/lib/events.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace fwyc::ui {

using namespace VSTGUI;

namespace {

void fillRoundRect (CDrawContext* context, const CRect& r, CCoord radius, const CColor& fill)
{
    if (auto path = owned (context->createRoundRectGraphicsPath (r, radius)))
    {
        context->setFillColor (fill);
        context->drawGraphicsPath (path, CDrawContext::kPathFilled);
    }
}

// 1 px border drawn inside r, like CSS border-box.
void strokeRoundRect (CDrawContext* context, const CRect& r, CCoord radius, const CColor& stroke)
{
    CRect inner = r;
    inner.inset (0.5, 0.5);
    if (auto path = owned (context->createRoundRectGraphicsPath (inner, radius - 0.5)))
    {
        context->setFrameColor (stroke);
        context->setLineWidth (1.0);
        context->setLineStyle (kLineSolid);
        context->drawGraphicsPath (path, CDrawContext::kPathStroked);
    }
}

void fillCircle (CDrawContext* context, const CPoint& centre, CCoord radius, const CColor& fill)
{
    context->setFillColor (fill);
    context->drawEllipse (CRect (centre.x - radius, centre.y - radius, centre.x + radius, centre.y + radius),
                          kDrawFilled);
}

void drawText (CDrawContext* context, const std::string& text, const CRect& r, CFontDesc* font, const CColor& color,
               CHoriTxtAlign align = kLeftText)
{
    context->setFont (font);
    context->setFontColor (color);
    context->drawString (text.c_str (), r, align, true);
}

std::string formatHz (double hz)
{
    char text[32];
    if (hz < 1000.0)
        std::snprintf (text, sizeof (text), "%.0f Hz", hz);
    else
        std::snprintf (text, sizeof (text), "%.1f kHz", hz / 1000.0);
    return text;
}

std::string formatDb (double db)
{
    char text[32];
    std::snprintf (text, sizeof (text), "%.0f dB", std::abs (db) < 0.5 ? 0.0 : db);
    return text;
}

// Down arrow onto a line: "attenuation".
void drawCutIcon (CDrawContext* context, const CPoint& topLeft, const CColor& color)
{
    const CCoord x = topLeft.x, y = topLeft.y;
    context->setFrameColor (color);
    context->setLineWidth (1.5);
    context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
    context->drawLine ({x + 6, y + 1.5}, {x + 6, y + 8.5});
    context->drawLine ({x + 3, y + 6}, {x + 6, y + 9});
    context->drawLine ({x + 6, y + 9}, {x + 9, y + 6});
    context->drawLine ({x + 2, y + 11}, {x + 10, y + 11});
}

} // namespace

//------------------------------------------------------------------------
Panel::Panel (const CRect& size, CColor fill, CColor border, CCoord radius)
: CView (size), fill_ (fill), border_ (border), radius_ (radius)
{
    setMouseEnabled (false);
}

void Panel::draw (CDrawContext* context)
{
    fillRoundRect (context, getViewSize (), radius_, fill_);
    strokeRoundRect (context, getViewSize (), radius_, border_);
    setDirty (false);
}

//------------------------------------------------------------------------
Label::Label (const CRect& size, std::string text, SharedPointer<CFontDesc> font, CColor color, CHoriTxtAlign align)
: CView (size), text_ (std::move (text)), font_ (std::move (font)), color_ (color), align_ (align)
{
    setMouseEnabled (false);
}

void Label::setText (const std::string& text)
{
    if (text != text_)
    {
        text_ = text;
        invalid ();
    }
}

void Label::setColor (CColor color)
{
    if (color != color_)
    {
        color_ = color;
        invalid ();
    }
}

void Label::draw (CDrawContext* context)
{
    drawText (context, text_, getViewSize (), font_, color_, align_);
    setDirty (false);
}

//------------------------------------------------------------------------
StatusPill::StatusPill (const CRect& size, bool outlined, CHoriTxtAlign align)
: CView (size), outlined_ (outlined), align_ (align), dot_ (theme::kIdle), textColor_ (theme::kMuted)
{
    setMouseEnabled (false);
}

void StatusPill::setStatus (const std::string& text, CColor dot, CColor textColor)
{
    if (text != text_ || dot != dot_ || textColor != textColor_)
    {
        text_ = text;
        dot_ = dot;
        textColor_ = textColor;
        invalid ();
    }
}

void StatusPill::draw (CDrawContext* context)
{
    const CRect r = getViewSize ();
    auto font = theme::uiFont (12);
    context->setFont (font);
    const CCoord textWidth = context->getStringWidth (text_.c_str ());

    CCoord x = r.left;
    if (outlined_)
    {
        strokeRoundRect (context, r, r.getHeight () / 2.0, theme::kBorder);
        x += 12;
    }
    else if (align_ == kRightText)
    {
        x = r.right - textWidth - 16;
    }

    fillCircle (context, {x + 4, r.getCenter ().y}, 4, dot_);
    drawText (context, text_, CRect (x + 16, r.top, r.right, r.bottom), font, textColor_);
    setDirty (false);
}

//------------------------------------------------------------------------
TextButton::TextButton (const CRect& size, IControlListener* listener, int32_t tag, std::string text,
                        std::string onText, bool toggle, bool editsParameter)
: CControl (size, listener, tag), text_ (std::move (text)), onText_ (onText.empty () ? text_ : std::move (onText)),
  toggle_ (toggle), editsParameter_ (editsParameter)
{
}

void TextButton::draw (CDrawContext* context)
{
    const CRect r = getViewSize ();
    const bool on = toggle_ && getValueNormalized () >= 0.5f;
    if (on)
    {
        fillRoundRect (context, r, 8, theme::kWarn);
        drawText (context, onText_, r, theme::uiFont (13, true), theme::kWarnInk, kCenterText);
    }
    else
    {
        if (pressed_)
            fillRoundRect (context, r, 8, theme::kRow);
        strokeRoundRect (context, r, 8, theme::kBorder);
        drawText (context, text_, r, theme::uiFont (13), toggle_ ? theme::kMuted : theme::kText, kCenterText);
    }
    setDirty (false);
}

void TextButton::onMouseDownEvent (MouseDownEvent& event)
{
    if (!event.buttonState.isLeft ())
        return;
    event.consumed = true;

    if (toggle_)
    {
        if (editsParameter_)
            beginEdit ();
        setValueNormalized (getValueNormalized () >= 0.5f ? 0.0f : 1.0f);
        valueChanged ();
        if (editsParameter_)
            endEdit ();
    }
    else
    {
        // Momentary action: fires once per click.
        setValueNormalized (1.0f);
        valueChanged ();
        setValueNormalized (0.0f);
    }
    invalid ();
}

//------------------------------------------------------------------------
SegmentedControl::SegmentedControl (const CRect& size, IControlListener* listener, int32_t tag,
                                    std::vector<std::string> labels, CCoord fontSize, bool editsParameter)
: CControl (size, listener, tag), labels_ (std::move (labels)), fontSize_ (fontSize), editsParameter_ (editsParameter)
{
}

int SegmentedControl::selectedIndex () const
{
    const int last = static_cast<int> (labels_.size ()) - 1;
    return std::clamp (static_cast<int> (std::lround (getValueNormalized () * last)), 0, last);
}

CRect SegmentedControl::segmentRect (int index) const
{
    CRect r = getViewSize ();
    r.inset (4, 4); // 1 px border + 3 px padding
    const CCoord width = r.getWidth () / static_cast<double> (labels_.size ());
    return CRect (r.left + index * width, r.top, r.left + (index + 1) * width, r.bottom);
}

void SegmentedControl::draw (CDrawContext* context)
{
    const CRect r = getViewSize ();
    fillRoundRect (context, r, 8, theme::kBackground);
    strokeRoundRect (context, r, 8, theme::kBorder);

    const int selected = selectedIndex ();
    for (int i = 0; i < static_cast<int> (labels_.size ()); ++i)
    {
        const CRect seg = segmentRect (i);
        if (i == selected)
        {
            fillRoundRect (context, seg, 6, theme::kAccent);
            drawText (context, labels_[static_cast<size_t> (i)], seg, theme::uiFont (fontSize_, true),
                      theme::kAccentInk, kCenterText);
        }
        else
        {
            drawText (context, labels_[static_cast<size_t> (i)], seg, theme::uiFont (fontSize_), theme::kMuted,
                      kCenterText);
        }
    }
    setDirty (false);
}

void SegmentedControl::onMouseDownEvent (MouseDownEvent& event)
{
    if (!event.buttonState.isLeft ())
        return;
    event.consumed = true;

    for (int i = 0; i < static_cast<int> (labels_.size ()); ++i)
    {
        if (!segmentRect (i).pointInside (event.mousePosition) || i == selectedIndex ())
            continue;
        if (editsParameter_)
            beginEdit ();
        setValueNormalized (labels_.size () > 1 ? static_cast<float> (i) / static_cast<float> (labels_.size () - 1) : 0.0f);
        valueChanged ();
        if (editsParameter_)
            endEdit ();
        invalid ();
        return;
    }
}

//------------------------------------------------------------------------
ArcKnob::ArcKnob (const CRect& size, IControlListener* listener, int32_t tag)
: CKnobBase (size, listener, tag, nullptr)
{
}

void ArcKnob::draw (CDrawContext* context)
{
    constexpr double kStartDeg = 135.0, kSweepDeg = 270.0;
    const CRect r = getViewSize ();
    const CPoint c = r.getCenter ();
    const CCoord radius = std::min (r.getWidth (), r.getHeight ()) / 2.0 - 6.0;
    const CRect arcRect (c.x - radius, c.y - radius, c.x + radius, c.y + radius);
    const double norm = std::clamp (static_cast<double> (getValueNormalized ()), 0.0, 1.0);

    context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
    context->setLineWidth (5.0);
    if (auto track = owned (context->createGraphicsPath ()))
    {
        track->addArc (arcRect, kStartDeg, kStartDeg + kSweepDeg, true);
        context->setFrameColor (theme::kTrack);
        context->drawGraphicsPath (track, CDrawContext::kPathStroked);
    }
    if (norm > 0.002)
    {
        if (auto arc = owned (context->createGraphicsPath ()))
        {
            arc->addArc (arcRect, kStartDeg, kStartDeg + kSweepDeg * norm, true);
            context->setFrameColor (theme::kAccent);
            context->drawGraphicsPath (arc, CDrawContext::kPathStroked);
        }
    }

    // Pointer: from the centre, -135 deg (min) to +135 deg (max) from straight up.
    const double angle = (-135.0 + kSweepDeg * norm) * std::numbers::pi / 180.0;
    const CCoord length = radius * 15.0 / 26.0;
    context->setLineWidth (2.5);
    context->setFrameColor (theme::kText);
    context->drawLine (c, {c.x + length * std::sin (angle), c.y - length * std::cos (angle)});
    setDirty (false);
}

//------------------------------------------------------------------------
LevelMeter::LevelMeter (const CRect& size, bool warm) : CView (size), warm_ (warm)
{
    setMouseEnabled (false);
}

void LevelMeter::setLevelDb (float db)
{
    if (std::abs (db - levelDb_) > 0.05f)
    {
        levelDb_ = db;
        invalid ();
    }
}

void LevelMeter::draw (CDrawContext* context)
{
    constexpr float kFloorDb = -60.0f;
    const CRect r = getViewSize ();
    const CCoord radius = r.getWidth () / 2.0;
    fillRoundRect (context, r, radius, theme::kBackground);

    const double fraction = std::clamp ((levelDb_ - kFloorDb) / -kFloorDb, 0.0f, 1.0f);
    if (fraction > 0.0)
    {
        const CRect filled (r.left, r.bottom - r.getHeight () * fraction, r.right, r.bottom);
        ConcatClip clip (*context, filled);
        if (auto path = owned (context->createRoundRectGraphicsPath (r, radius)))
        {
            if (warm_)
            {
                auto gradient = owned (CGradient::create (0.0, 1.0, theme::kAccent, theme::kWarn));
                gradient->addColorStop (0.7, theme::kAccent);
                context->fillLinearGradient (path, *gradient, CPoint (r.left, r.bottom), CPoint (r.left, r.top));
            }
            else
            {
                context->setFillColor (theme::kAccent);
                context->drawGraphicsPath (path, CDrawContext::kPathFilled);
            }
        }
    }
    setDirty (false);
}

//------------------------------------------------------------------------
NotchList::NotchList (const CRect& size) : CView (size)
{
    setMouseEnabled (false);
}

void NotchList::setNotches (const MeterFrame& frame, bool detectOnly)
{
    notches_.assign (frame.notches, frame.notches + std::clamp (frame.numNotches, 0, MeterFrame::kMaxNotches));
    detectOnly_ = detectOnly;
    invalid ();
}

void NotchList::draw (CDrawContext* context)
{
    constexpr CCoord kRowHeight = 52, kGap = 8;
    const CRect r = getViewSize ();

    if (notches_.empty ())
    {
        drawText (context, "No feedback detected", CRect (r.left, r.top, r.right, r.top + kRowHeight),
                  theme::uiFont (12), theme::kMuted, kCenterText);
        setDirty (false);
        return;
    }

    const int fits = std::max (1, static_cast<int> ((r.getHeight () + kGap) / (kRowHeight + kGap)));
    const int total = static_cast<int> (notches_.size ());
    const int shown = total > fits ? fits - 1 : total; // leave a row for "+N more"

    auto freqFont = theme::monoFont (14);
    auto valueFont = theme::monoFont (13);
    auto subFont = theme::uiFont (11);

    for (int i = 0; i < shown; ++i)
    {
        const auto& n = notches_[static_cast<size_t> (i)];
        const CRect row (r.left, r.top + i * (kRowHeight + kGap), r.right, r.top + i * (kRowHeight + kGap) + kRowHeight);
        fillRoundRect (context, row, 8, theme::kRow);

        const CRect inner = CRect (row).inset (12, 9);
        drawText (context, formatHz (n.frequencyHz), CRect (inner.left, inner.top, inner.right, inner.top + 18),
                  freqFont, theme::kText);

        std::string state;
        if (n.kind == MeterNotch::kActive)
        {
            char text[32];
            std::snprintf (text, sizeof (text), "held %.1f s", n.secondsEngaged);
            state = text;
        }
        else
        {
            state = n.kind == MeterNotch::kBuilding ? "building" : "releasing";
        }
        drawText (context, state, CRect (inner.left, inner.bottom - 15, inner.right, inner.bottom), subFont,
                  theme::kMuted);

        drawText (context, formatDb (n.levelDb), CRect (inner.left, inner.top - 1, inner.right, inner.top + 16),
                  valueFont, n.kind == MeterNotch::kReleasing ? theme::kMuted : theme::kWarn, kRightText);

        // A notch that is cutting shows teal; nothing to cut yet, or detect-only, shows grey.
        const bool cutting = n.depthDb > 0.05f && !detectOnly_;
        const CColor cutColor = cutting ? theme::kAccent : theme::kMuted;
        const std::string cut = formatDb (-n.depthDb);
        context->setFont (valueFont);
        const CCoord cutWidth = context->getStringWidth (cut.c_str ());
        const CRect cutRect (inner.left, inner.bottom - 17, inner.right, inner.bottom);
        drawText (context, cut, cutRect, valueFont, cutColor, kRightText);
        drawCutIcon (context, {inner.right - cutWidth - 16, cutRect.getCenter ().y - 6}, cutColor);
    }

    if (shown < total)
    {
        char text[32];
        std::snprintf (text, sizeof (text), "+%d more", total - shown);
        const CCoord top = r.top + shown * (kRowHeight + kGap);
        drawText (context, text, CRect (r.left, top, r.right, top + 24), subFont, theme::kMuted, kCenterText);
    }
    setDirty (false);
}

//------------------------------------------------------------------------
Analyser::Analyser (const CRect& size) : CView (size)
{
    setMouseEnabled (false);
    displayDb_.fill (-120.0f);
    rowDb_.fill (-120.0f);
    rows_.assign (static_cast<size_t> (kRows * kBands), theme::kWell);
}

void Analyser::setMode (Mode mode)
{
    if (mode != mode_)
    {
        mode_ = mode;
        invalid ();
    }
}

void Analyser::push (const MeterFrame& frame)
{
    analysedMinHz_ = frame.analysedMinHz;
    analysedMaxHz_ = frame.analysedMaxHz;
    notches_.assign (frame.notches, frame.notches + std::clamp (frame.numNotches, 0, MeterFrame::kMaxNotches));

    // Spectrum: instant rise, eased fall, so the curve reads steadily.
    for (int i = 0; i < kBands; ++i)
    {
        const float db = frame.spectrumDb[i];
        float& shown = displayDb_[static_cast<size_t> (i)];
        shown = db > shown ? db : shown + 0.3f * (db - shown);
    }

    // Waterfall: loudest value per band over the row's frames, feedback bands marked.
    for (int i = 0; i < kBands; ++i)
        rowDb_[static_cast<size_t> (i)] = std::max (rowDb_[static_cast<size_t> (i)], frame.spectrumDb[i]);
    const double bandsPerDecade = kBands / std::log10 (MeterFrame::kSpectrumMaxHz / MeterFrame::kSpectrumMinHz);
    for (const auto& n : notches_)
    {
        if (n.kind != MeterNotch::kActive || n.frequencyHz <= 0.0f)
            continue;
        const int band = static_cast<int> (std::log10 (n.frequencyHz / MeterFrame::kSpectrumMinHz) * bandsPerDecade);
        for (int b = band - 1; b <= band + 1; ++b)
            if (b >= 0 && b < kBands)
                rowFeedback_[static_cast<size_t> (b)] = true;
    }
    if (++framesInRow_ >= kFramesPerRow)
        commitRow ();

    invalid ();
}

void Analyser::commitRow ()
{
    // Scroll up one row; the newest row is at the bottom ("now").
    std::move (rows_.begin () + kBands, rows_.end (), rows_.begin ());
    CColor* row = rows_.data () + static_cast<size_t> ((kRows - 1) * kBands);
    for (int i = 0; i < kBands; ++i)
    {
        const double t = std::clamp ((rowDb_[static_cast<size_t> (i)] + 96.0) / 72.0, 0.0, 1.0);
        row[i] = rowFeedback_[static_cast<size_t> (i)] ? theme::mix (theme::kWarn, theme::kHot, t)
                                                       : theme::mix (theme::kWell, theme::kAccent, std::pow (t, 1.4) * 0.85);
    }
    rowDb_.fill (-120.0f);
    rowFeedback_.fill (false);
    framesInRow_ = 0;
    bitmapDirty_ = true;
}

CRect Analyser::plotRect () const
{
    const CRect r = getViewSize ();
    return CRect (r.left + 34, r.top, r.right, r.bottom - 22);
}

CCoord Analyser::xForHz (const CRect& plot, double hz) const
{
    const double t = std::log10 (std::max (hz, 1.0) / MeterFrame::kSpectrumMinHz) /
                     std::log10 (MeterFrame::kSpectrumMaxHz / MeterFrame::kSpectrumMinHz);
    return plot.left + plot.getWidth () * std::clamp (t, 0.0, 1.0);
}

void Analyser::draw (CDrawContext* context)
{
    const CRect plot = plotRect ();
    if (mode_ == Mode::kSpectrum)
        drawSpectrum (context, plot);
    else
        drawWaterfall (context, plot);
    drawFrequencyAxis (context, plot);
    setDirty (false);
}

void Analyser::drawSpectrum (CDrawContext* context, const CRect& plot)
{
    auto axisFont = theme::monoFont (10);

    // Level axis and grid.
    const char* levels[] = {"0", "-30", "-60", "-90"};
    context->setLineWidth (1.0);
    context->setLineStyle (kLineSolid);
    context->setFrameColor (theme::kPanelBorder);
    for (int i = 0; i < 4; ++i)
    {
        const CCoord y = std::round (plot.top + (plot.getHeight () - 1) * i / 3.0) + 0.5;
        context->drawLine ({plot.left, y}, {plot.right, y});
        const CRect labelRect (plot.left - 34, y - (i == 0 ? 0 : i == 3 ? 12 : 6), plot.left - 6,
                               y + (i == 0 ? 12 : i == 3 ? 0 : 6));
        drawText (context, levels[i], labelRect, axisFont, theme::kDim, kRightText);
    }
    for (double hz : {100.0, 1000.0, 10000.0})
    {
        const CCoord x = std::round (xForHz (plot, hz)) + 0.5;
        context->drawLine ({x, plot.top}, {x, plot.bottom});
    }

    // Curve, filled beneath with a fading gradient.
    {
        ConcatClip clip (*context, plot);
        auto yFor = [&] (float db) {
            const double t = std::clamp ((kTopDb - db) / (kTopDb - kBottomDb), 0.0f, 1.0f);
            return plot.top + plot.getHeight () * t;
        };
        auto line = owned (context->createGraphicsPath ());
        auto area = owned (context->createGraphicsPath ());
        if (line && area)
        {
            for (int i = 0; i < kBands; ++i)
            {
                const CPoint p (plot.left + plot.getWidth () * (i + 0.5) / kBands, yFor (displayDb_[static_cast<size_t> (i)]));
                if (i == 0)
                {
                    line->beginSubpath (p);
                    area->beginSubpath (CPoint (plot.left, plot.bottom));
                    area->addLine (CPoint (plot.left, p.y));
                }
                line->addLine (p);
                area->addLine (p);
            }
            area->addLine (CPoint (plot.right, yFor (displayDb_[kBands - 1])));
            area->addLine (CPoint (plot.right, plot.bottom));
            area->closeSubpath ();

            auto gradient = owned (CGradient::create (0.0, 1.0, theme::withAlpha (theme::kAccent, 0.35),
                                                      theme::withAlpha (theme::kAccent, 0.0)));
            context->fillLinearGradient (area, *gradient, CPoint (plot.left, plot.top), CPoint (plot.left, plot.bottom));

            context->setLineWidth (1.5);
            context->setLineStyle (CLineStyle (CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
            context->setFrameColor (theme::kAccent);
            context->drawGraphicsPath (line, CDrawContext::kPathStroked);
        }
    }

    drawUnanalysed (context, plot, 6);
    drawNotchMarkers (context, plot, true);
}

void Analyser::drawWaterfall (CDrawContext* context, const CRect& plot)
{
    auto axisFont = theme::monoFont (10);
    drawText (context, "-20s", CRect (plot.left - 34, plot.top, plot.left - 6, plot.top + 12), axisFont, theme::kDim,
              kRightText);
    drawText (context, "-10s", CRect (plot.left - 34, plot.getCenter ().y - 6, plot.left - 6, plot.getCenter ().y + 6),
              axisFont, theme::kDim, kRightText);
    drawText (context, "now", CRect (plot.left - 34, plot.bottom - 12, plot.left - 6, plot.bottom), axisFont, theme::kDim,
              kRightText);

    fillRoundRect (context, plot, 6, theme::kWell);
    {
        CRect inner = plot;
        inner.inset (1, 1);
        ConcatClip clip (*context, inner);

        if (!bitmap_)
            bitmap_ = makeOwned<CBitmap> (static_cast<CCoord> (kBands), static_cast<CCoord> (kRows));
        if (bitmapDirty_)
        {
            if (auto pixels = owned (CBitmapPixelAccess::create (bitmap_, false)))
            {
                for (int y = 0; y < kRows; ++y)
                    for (int x = 0; x < kBands; ++x)
                    {
                        pixels->setPosition (static_cast<uint32_t> (x), static_cast<uint32_t> (y));
                        pixels->setColor (rows_[static_cast<size_t> (y * kBands + x)]);
                    }
            }
            bitmapDirty_ = false;
        }

        // Stretch the band x row bitmap over the plot.
        const double sx = inner.getWidth () / kBands, sy = inner.getHeight () / kRows;
        context->setBitmapInterpolationQuality (BitmapInterpolationQuality::kMedium);
        {
            CDrawContext::Transform transform (*context, CGraphicsTransform ().scale (sx, sy));
            const CRect dest (inner.left / sx, inner.top / sy, inner.left / sx + kBands, inner.top / sy + kRows);
            context->drawBitmap (bitmap_, dest);
        }

        context->setLineWidth (1.0);
        context->setLineStyle (kLineSolid);
        context->setFrameColor (theme::withAlpha (theme::kText, 0.06));
        for (double hz : {100.0, 1000.0, 10000.0})
        {
            const CCoord x = std::round (xForHz (plot, hz)) + 0.5;
            context->drawLine ({x, plot.top}, {x, plot.bottom});
        }
        drawUnanalysed (context, plot, 8);
    }
    strokeRoundRect (context, plot, 6, theme::kPanelBorder);
    drawNotchMarkers (context, plot, false);
}

void Analyser::drawUnanalysed (CDrawContext* context, const CRect& plot, CCoord labelInset)
{
    auto hatch = [&] (const CRect& area, bool edgeOnRight) {
        if (area.getWidth () < 1.0)
            return;
        ConcatClip clip (*context, area);
        context->setFillColor (theme::withAlpha (theme::kBackground, 0.64));
        context->drawRect (area, kDrawFilled);

        context->setFrameColor (theme::withAlpha (theme::kBackground, 0.5));
        context->setLineWidth (2.8);
        context->setLineStyle (kLineSolid);
        const CCoord h = area.getHeight ();
        for (CCoord x = area.left - h; x < area.right; x += 8.0)
            context->drawLine ({x, area.bottom}, {x + h, area.top});

        context->setFrameColor (theme::kDim);
        context->setLineWidth (1.0);
        const CCoord edge = edgeOnRight ? area.right - 0.5 : area.left + 0.5;
        context->drawLine ({edge, area.top}, {edge, area.bottom});
    };

    const CRect low (plot.left, plot.top, std::round (xForHz (plot, analysedMinHz_)), plot.bottom);
    const CRect high (std::round (xForHz (plot, analysedMaxHz_)), plot.top, plot.right, plot.bottom);
    hatch (low, true);
    hatch (high, false);

    if (low.getWidth () > 60.0)
        drawText (context, "not analysed", CRect (low.left, low.bottom - labelInset - 12, low.right, low.bottom - labelInset),
                  theme::monoFont (10), theme::kDim, kCenterText);
}

void Analyser::drawNotchMarkers (CDrawContext* context, const CRect& plot, bool lines)
{
    auto chipFont = theme::monoFont (10);
    context->setFont (chipFont);

    // Left to right, so chips that would overlap can step down a row.
    std::vector<MeterNotch> marked;
    for (const auto& n : notches_)
        if (n.kind != MeterNotch::kBuilding && n.depthDb > 0.05f)
            marked.push_back (n);
    std::sort (marked.begin (), marked.end (),
               [] (const MeterNotch& a, const MeterNotch& b) { return a.frequencyHz < b.frequencyHz; });
    constexpr int kChipRows = 3;
    std::array<CCoord, kChipRows> rowRight;
    rowRight.fill (-1e9);

    for (const auto& n : marked)
    {
        const CCoord x = std::round (xForHz (plot, n.frequencyHz)) + 0.5;

        if (lines)
        {
            const CCoord dashes[] = {3.0, 3.0};
            context->setLineStyle (CLineStyle (CLineStyle::kLineCapButt, CLineStyle::kLineJoinMiter, 0, 2, dashes));
            context->setLineWidth (1.0);
            context->setFrameColor (n.kind == MeterNotch::kActive ? theme::kWarn : theme::withAlpha (theme::kWarn, 0.45));
            context->drawLine ({x, plot.top}, {x, plot.bottom});
        }
        // In the waterfall, feedback already shows in its history as hot cells; only the chip is added.

        const std::string label = formatHz (n.frequencyHz);
        context->setFont (chipFont);
        const CCoord width = context->getStringWidth (label.c_str ()) + 12;
        const CCoord left = std::clamp (x - width / 2.0, plot.left, plot.right - width);
        int row = 0;
        while (row < kChipRows - 1 && left < rowRight[static_cast<size_t> (row)] + 4)
            ++row;
        rowRight[static_cast<size_t> (row)] = left + width;
        const CCoord top = plot.top + 6 + row * 21;
        const CRect chip (left, top, left + width, top + 17);
        fillRoundRect (context, chip, 4, n.kind == MeterNotch::kActive ? theme::kWarn : theme::withAlpha (theme::kWarn, 0.55));
        drawText (context, label, chip, chipFont, theme::kWarnInk, kCenterText);
    }
    context->setLineStyle (kLineSolid);
}

void Analyser::drawFrequencyAxis (CDrawContext* context, const CRect& plot)
{
    auto axisFont = theme::monoFont (10);
    const CCoord top = plot.bottom + 8, bottom = plot.bottom + 22;
    drawText (context, "20", CRect (plot.left, top, plot.left + 40, bottom), axisFont, theme::kDim, kLeftText);
    drawText (context, "20k", CRect (plot.right - 40, top, plot.right, bottom), axisFont, theme::kDim, kRightText);
    const std::pair<double, const char*> ticks[] = {{100.0, "100"}, {1000.0, "1k"}, {10000.0, "10k"}};
    for (const auto& [hz, text] : ticks)
    {
        const CCoord x = xForHz (plot, hz);
        drawText (context, text, CRect (x - 20, top, x + 20, bottom), axisFont, theme::kDim, kCenterText);
    }
}

} // namespace fwyc::ui
