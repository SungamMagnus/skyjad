#include "PluginEditor.h"

using namespace cld;
using namespace cld::panel;

namespace
{
constexpr float kTrigFlashMs = 110.0f;

/* Grain: five faders, pitch first and bipolar around unison. */
constexpr int kGrainFaderParam[numGrain] = { 2, 0, 1, 3, 4 };
const char* kGrainLabel[numGrain] = { "PITCH", "POSITION", "SIZE", "DENSITY", "TEXTURE" };

/* Blend: four values, four faders, no shared knob anywhere. */
const char* kBlendLabel[numBlend] = { "DRY/WET", "SPREAD", "FEEDBACK", "REVERB" };

/* Control indices into `ctls`, in build order. */
constexpr int kGrainFader = 0;
constexpr int kNoteTrack  = 5;
constexpr int kMode       = 6;
constexpr int kQuality    = 7;
constexpr int kFreeze     = 8;
constexpr int kLimiter    = 9;
constexpr int kBlendFader = 10;
} // namespace

CloudiusEditor::CloudiusEditor (CloudiusProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setOpaque (true);
    buildControls();

    setResizable (true, true);
    setResizeLimits (640, (int) (640.0f * designH / designW),
                     1540, (int) (1540.0f * designH / designW));
    getConstrainer()->setFixedAspectRatio ((double) designW / (double) designH);
    setSize ((int) designW, (int) designH);

    startTimerHz (30);
}

CloudiusEditor::~CloudiusEditor() = default;

/* ── Control table ───────────────────────────────────────────────────────── */

void CloudiusEditor::buildControls()
{
    auto add = [this] (Kind kind, const juce::String& id, juce::Rectangle<float> hit,
                       float travel, int positions = 0, bool vertical = false)
    {
        Ctl c;
        c.kind = kind;
        c.param = proc.apvts.getParameter (id);
        c.hit = hit;
        c.travel = travel;
        c.positions = positions;
        c.vertical = vertical;
        jassert (c.param != nullptr);
        ctls.push_back (c);
    };

    auto faderBox = [] (float cx)
    {
        return juce::Rectangle<float> (cx - 18.0f, faderTop - 8.0f, 36.0f, faderTrack + 16.0f);
    };

    /* Grain. */
    for (int i = 0; i < numGrain; ++i)
        add (Kind::fader, pid::grain[kGrainFaderParam[i]],
             faderBox (faderX (grainX, i)), faderTrack);
    add (Kind::selector, pid::noteTrack, noteRect(), 0.0f, 2);

    /* Engine. */
    add (Kind::selector, pid::mode,    modeRect(),    0.0f, 4, true);
    add (Kind::selector, pid::quality, qualityRect(), 0.0f, 4, true);
    add (Kind::latch,    pid::freeze,  freezeRect(),  0.0f, 2);

    add (Kind::latch, pid::limiter, limiterBox().expanded (6.0f), 0.0f, 2);

    /* Blend. */
    for (int i = 0; i < numBlend; ++i)
        add (Kind::fader, pid::blend[i], faderBox (faderX (blendX, i)), faderTrack);
}

float CloudiusEditor::scale() const { return (float) getWidth() / designW; }

juce::Point<float> CloudiusEditor::toDesign (juce::Point<float> px) const
{
    const float k = juce::jmax (0.0001f, scale());
    return { px.x / k, px.y / k };
}

int CloudiusEditor::controlAt (juce::Point<float> design) const
{
    for (size_t i = 0; i < ctls.size(); ++i)
        if (ctls[i].hit.contains (design))
            return (int) i;
    return -1;
}

int CloudiusEditor::indexOf (const juce::RangedAudioParameter& p, int positions)
{
    return juce::jlimit (0, positions - 1,
                         juce::roundToInt (p.getValue() * (float) (positions - 1)));
}

/* ── Interaction ─────────────────────────────────────────────────────────── */

void CloudiusEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto d = toDesign (e.position);

    if (trigRect().contains (d))
    {
        proc.requestTrigger();
        trigHeld = true;
        repaint();
        return;
    }

    const int idx = controlAt (d);
    if (idx < 0)
        return;

    auto& c = ctls[(size_t) idx];

    if (c.kind == Kind::selector || c.kind == Kind::latch)
    {
        const int n = juce::jmax (2, c.positions);
        const float along = c.vertical ? (d.y - c.hit.getY()) / (c.hit.getHeight() / (float) n)
                                       : (d.x - c.hit.getX()) / (c.hit.getWidth()  / (float) n);
        const int seg = juce::jlimit (0, n - 1, (int) along);
        c.param->beginChangeGesture();
        c.param->setValueNotifyingHost (c.kind == Kind::latch
                                            ? (c.param->getValue() > 0.5f ? 0.0f : 1.0f)
                                            : (float) seg / (float) (n - 1));
        c.param->endChangeGesture();
        repaint();
        return;
    }

    dragIdx = idx;
    dragStartNorm = c.param->getValue();
    dragStartY = d.y;
    c.param->beginChangeGesture();
    setMouseCursor (juce::MouseCursor::NoCursor);
}

void CloudiusEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIdx < 0)
        return;

    auto& c = ctls[(size_t) dragIdx];
    const float sensitivity = e.mods.isShiftDown() ? 0.22f : 1.0f;
    const float delta = (dragStartY - toDesign (e.position).y) / c.travel;

    c.param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f,
                                                  dragStartNorm + delta * sensitivity));
    repaint();
}

void CloudiusEditor::mouseUp (const juce::MouseEvent&)
{
    if (trigHeld)
    {
        trigHeld = false;
        repaint();
    }

    if (dragIdx < 0)
        return;

    ctls[(size_t) dragIdx].param->endChangeGesture();
    dragIdx = -1;
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void CloudiusEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int idx = controlAt (toDesign (e.position));
    if (idx < 0)
        return;

    auto& c = ctls[(size_t) idx];
    if (c.kind == Kind::selector || c.kind == Kind::latch)
        return;

    c.param->beginChangeGesture();
    c.param->setValueNotifyingHost (c.param->getDefaultValue());
    c.param->endChangeGesture();
    repaint();
}

void CloudiusEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int idx = controlAt (toDesign (e.position));
    if (idx < 0)
        return;

    auto& c = ctls[(size_t) idx];
    if (c.kind == Kind::latch)
        return;

    /* A notch is deltaY ~0.1: one position on a selector, ~1.6% elsewhere. */
    const float gain = c.kind == Kind::selector
                           ? 1.0f / (0.1f * (float) juce::jmax (1, c.positions - 1))
                           : (e.mods.isShiftDown() ? 0.04f : 0.16f);
    const float delta = w.deltaY * (w.isReversed ? -1.0f : 1.0f) * gain;

    c.param->beginChangeGesture();
    c.param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, c.param->getValue() + delta));
    c.param->endChangeGesture();
    repaint();
}

void CloudiusEditor::timerCallback()
{
    const int trigs = proc.panel.trigCount.load (std::memory_order_relaxed);
    if (trigs != lastTrigCount)
    {
        lastTrigCount = trigs;
        trigFlashMs = juce::Time::getMillisecondCounter();
    }
    repaint();
}

/* ── Paint ───────────────────────────────────────────────────────────────── */

void CloudiusEditor::paint (juce::Graphics& g)
{
    g.fillAll (hue::paper);
    g.addTransform (juce::AffineTransform::scale (scale()));

    const auto& state = proc.panel;
    const int mode    = indexOf (*ctls[(size_t) kMode].param, 4);
    const int quality = indexOf (*ctls[(size_t) kQuality].param, 4);
    const bool frozen = ctls[(size_t) kFreeze].param->getValue() > 0.5f;
    const bool flashing = juce::Time::getMillisecondCounter() - trigFlashMs
                              < (juce::uint32) kTrigFlashMs;

    g.setColour (ink (0.22f));
    g.drawRect (juce::Rectangle<float> (16.0f, 16.0f, designW - 32.0f, designH - 32.0f), 1.0f);

    rule (g, rule1X, ruleTop, 1.0f, ruleBot - ruleTop, 0.11f);
    rule (g, rule2X, ruleTop, 1.0f, ruleBot - ruleTop, 0.11f);

    /* Section titles claim the top line of their own column. */
    auto sectionTitle = [&g] (const char* t, float x, juce::Colour c)
    {
        tracked (g, t, { x, titleY, 200.0f, 12.0f }, titleSize, c, titleTracking);
    };
    sectionTitle ("GRAIN",  grainX,  hue::grain);
    sectionTitle ("ENGINE", engineL, hue::engine);
    sectionTitle ("BLEND",  blendX,  hue::blend);


    /* ── Grain ───────────────────────────────────────────────────────────── */
    {
        text (g, "NOTE", noteLabel(), 8.0f, ink (0.42f),
              juce::Justification::right, false);
        selector (g, noteRect(), indexOf (*ctls[(size_t) kNoteTrack].param, 2), hue::grain,
                  { "OFF", "TRACK" }, 8.0f);

        for (int i = 0; i < numGrain; ++i)
        {
            const int p = kGrainFaderParam[i];
            auto* param = ctls[(size_t) (kGrainFader + i)].param;
            const float v = param->convertFrom0to1 (param->getValue());
            fader (g, faderX (grainX, i), param->getValue(), hue::grain, kGrainLabel[i],
                   grainReadout (p, v, mode), p == 2);
        }
    }

    /* ── Engine ──────────────────────────────────────────────────────────── */
    {
        auto header = [&g] (const char* t, juce::Rectangle<float> over, float y)
        {
            tracked (g, t, over.withY (y).withHeight (11.0f), 8.0f, ink (0.55f), 0.6f, false);
        };
        auto caption = [&g] (const juce::String& t, float y)
        {
            text (g, t, engineCaption (y), 7.4f, ink (0.34f),
                  juce::Justification::centred, false);
        };

        header ("MODE", modeRect(), 62.0f);
        selectorVertical (g, modeRect(), mode, hue::engine,
                          { modeName[0], modeName[1], modeName[2], modeName[3] }, 7.6f);

        header ("QUALITY", qualityRect(), 62.0f);
        selectorVertical (g, qualityRect(), quality, hue::engine,
                          { qualityName[0], qualityName[1], qualityName[2], qualityName[3] }, 7.6f);

        caption (modeCaption[mode], 154.0f);
        caption (qualityCaption[quality], 166.0f);

        header ("BUFFER", bufferRect(), 182.0f);
        {
            /* Bank order is pitch, position, size, density, texture. */
            auto* pos  = ctls[(size_t) (kGrainFader + 1)].param;
            auto* size = ctls[(size_t) (kGrainFader + 2)].param;
            const float seconds = state.bufferSeconds.load (std::memory_order_relaxed);

            /* Only granular has a read window this panel can state exactly; the
               other engines get the write head and nothing invented. */
            const float window = mode == 0 && seconds > 0.0f
                                     ? grainSizeMs (size->convertFrom0to1 (size->getValue()))
                                           * 0.001f / seconds
                                     : -1.0f;

            bufferStrip (g, bufferRect(), state.head.load (std::memory_order_relaxed),
                         pos->getValue(), window, frozen, mode != 3, hue::engine);

            caption (mode == 3 ? juce::String ("spectral mode analyses live")
                               : juce::String (seconds, 2) + " s recorded"
                                     + (frozen ? " - held" : ""), 218.0f);
        }

        button (g, freezeRect(), frozen, hue::freeze, "FREEZE");
        button (g, trigRect(), trigHeld || flashing, hue::engine, "TRIG");
        text (g, "midi note", { engineL, 42.0f, engineW - 72.0f, 11.0f }, 7.0f,
              ink (0.32f), juce::Justification::right, false);
    }

    /* ── Blend ───────────────────────────────────────────────────────────── */
    {
        for (int i = 0; i < numBlend; ++i)
        {
            auto* param = ctls[(size_t) (kBlendFader + i)].param;
            fader (g, faderX (blendX, i), param->getValue(), hue::blend, kBlendLabel[i],
                   blendReadout (i, param->convertFrom0to1 (param->getValue())));
        }
    }

    /* ── Status bar — every item is live ─────────────────────────────────── */
    {
        rule (g, contentL, statusRuleY, contentR - contentL, 1.0f, 0.18f);

        const float y = statusY;
        const double hostRate = proc.getSampleRate();

        text (g, "VU", { 40.0f, y - 7.0f, 20.0f, 14.0f }, 8.0f, ink (0.45f),
              juce::Justification::left, false);
        ledBar (g, 62.0f, y, state.envIn.load (std::memory_order_relaxed) * 3.0f, hue::signal);

        /* The output meter carries the limiter's state - lit when it is
           guarding the output, grey when the output is running free. */
        const bool limiting = ctls[(size_t) kLimiter].param->getValue() > 0.5f;

        text (g, "OUT", { 124.0f, y - 7.0f, 26.0f, 14.0f }, 8.0f, ink (0.45f),
              juce::Justification::left, false);
        meter (g, outMeterRect(), state.envOut.load (std::memory_order_relaxed) * 3.0f,
               limiting ? hue::signal : ink (0.30f));
        checkbox (g, limiterBox(), limiting, hue::signal);
        text (g, "LIM", { 234.0f, y - 7.0f, 24.0f, 14.0f }, 7.6f,
              limiting ? hue::signal : ink (0.38f), juce::Justification::left, false);

        text (g, "CLIP", { 272.0f, y - 7.0f, 30.0f, 14.0f }, 8.0f, ink (0.45f),
              juce::Justification::left, false);
        lamp (g, 310.0f, y, state.inPeak.load (std::memory_order_relaxed) > 0.99f, hue::clip);

        text (g, "RATE", { 330.0f, y - 7.0f, 34.0f, 14.0f }, 8.0f, ink (0.45f),
              juce::Justification::left, false);
        text (g, hostRate > 0.0 ? "32 kHz from " + juce::String (hostRate / 1000.0, 1) + " kHz"
                                : juce::String ("32 kHz"),
              { 360.0f, y - 7.0f, 112.0f, 14.0f }, 8.5f, hue::ink, juce::Justification::left);

        text (g, "LATENCY", { 478.0f, y - 7.0f, 58.0f, 14.0f }, 8.0f, ink (0.45f),
              juce::Justification::left, false);
        text (g, hostRate > 0.0
                     ? juce::String (proc.getLatencySamples() * 1000.0 / hostRate, 1) + " ms"
                     : juce::String ("-"),
              { 534.0f, y - 7.0f, 58.0f, 14.0f }, 8.5f, hue::ink, juce::Justification::left);

        /* Bottom-right corner: the credit, then the mark. */
        text (g, "after clouds", { 606.0f, y - 7.0f, 84.0f, 14.0f }, 7.4f, ink (0.32f),
              juce::Justification::right, false);
        tracked (g, juce::CharPointer_UTF8 ("SKÝJAÐ"), { contentR - 70.0f, y - 6.0f, 70.0f, 12.0f }, 8.5f,
                 ink (0.62f), 3.2f);
    }
}
