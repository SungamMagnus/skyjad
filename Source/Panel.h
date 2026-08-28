#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Flat drawing primitives and the panel grid. Everything is expressed in a
 * fixed 920 x 432 design space; the editor applies one scale transform, so
 * layout constants double as hit-test geometry.
 *
 * Aluminium and black print, the way the module is silkscreened. The three
 * accents are the module's own: its four bi-colour LEDs only ever glow green,
 * amber or red, so those are the only hues the panel gets. A colour means
 * "this control belongs to that section", or it is a live signal - never
 * decoration.
 */
namespace cld::panel
{

constexpr float designW = 810.0f, designH = 294.0f;

/* ── Palette ─────────────────────────────────────────────────────────────── */
namespace hue
{
const juce::Colour paper  { 0xffeef0ec };   // brushed aluminium
const juce::Colour ink    { 0xff15181b };
const juce::Colour grain  { 0xff2e7d52 };   // LED green
const juce::Colour engine { 0xffc17d1f };   // LED amber
const juce::Colour blend  { 0xffab3b2c };   // LED red
const juce::Colour freeze { 0xffd6452f };   // LED red, lit
const juce::Colour signal { 0xff2e7d52 };
const juce::Colour clip   { 0xffc02617 };
}

inline juce::Colour ink (float alpha) { return hue::ink.withAlpha (alpha); }

/* ── Grid ────────────────────────────────────────────────────────────────
 * Nine faders and one selector bank, and nothing sitting above them: pitch is
 * bipolar rather than a different shape, so every column starts on the title
 * line and the travel gets the height a knob band used to take. Section titles
 * share that line, which is otherwise blank - as does note tracking, the one
 * control with nowhere else to be.
 */
constexpr float contentL = 40.0f, contentR = 770.0f;
constexpr float rule1X = 322.0f, rule2X = 538.0f;
constexpr float ruleTop = 20.0f, ruleBot = 240.0f;

constexpr float titleY = 22.0f, titleSize = 9.5f, titleTracking = 3.0f;

constexpr float faderStep  = 50.0f;
constexpr float faderTop   = 78.0f;
constexpr float faderTrack = 130.0f;
constexpr float faderBot   = faderTop + faderTrack;

/* Five faders left, four right, each bank the same distance in from its border.
   The engine column holds selectors and a readout rather than travel, so it is
   the one that can afford to be narrow. */
constexpr float grainX = 44.0f, blendX = 566.0f;
constexpr float engineL = 344.0f, engineW = 172.0f;

constexpr float statusY = 264.0f, statusRuleY = 248.0f;

inline float faderX (float columnX, int i) { return columnX + faderStep * (0.5f + (float) i); }

/** Four-position selector rows in the engine column. */
/* Mode and quality are four-way switches read as lists, side by side: stacked
   they take one row-pair between them instead of two full-width strips, and
   nothing has to be abbreviated to fit. */
constexpr float listW = 78.0f, listRow = 17.0f;
inline juce::Rectangle<float> modeRect()    { return { engineL, 80.0f, listW, listRow * 4.0f }; }
inline juce::Rectangle<float> qualityRect() { return { engineL + engineW - listW, 80.0f, listW, listRow * 4.0f }; }

/** Captions run the full section, which is wider than either list. */
inline juce::Rectangle<float> engineCaption (float y) { return { rule1X + 4.0f, y, rule2X - rule1X - 8.0f, 11.0f }; }

/* The buffer is a readout, not a control - a thin band is enough to place the
   recording head against the window, and it keeps the selectors' width. */
inline juce::Rectangle<float> bufferRect()  { return { engineL, 196.0f, engineW, 16.0f }; }

/* Freeze and Trig stack at the right of the engine title line rather than
   sitting side by side: two rows of one cost no height the title line was not
   already using, and the column stops being as wide as the pair of them. */
inline juce::Rectangle<float> freezeRect()  { return { engineL + engineW - 68.0f, 20.0f, 68.0f, 16.0f }; }
inline juce::Rectangle<float> trigRect()    { return { engineL + engineW - 68.0f, 40.0f, 68.0f, 16.0f }; }

/** The limiter rides the blend column's title line, as note tracking and the
    two latches ride theirs. */
inline juce::Rectangle<float> limiterRect() { return { contentR - 62.0f, 20.0f, 62.0f, 16.0f }; }

/** Note tracking rides the grain column's title line, beside the title. */
inline juce::Rectangle<float> noteRect()  { return { 214.0f, 20.0f, 80.0f, 17.0f }; }
inline juce::Rectangle<float> noteLabel() { return { 156.0f, 20.0f, 50.0f, 17.0f }; }

/* ── Type ────────────────────────────────────────────────────────────────── */
juce::Font mono (float h, bool bold = false);

void text (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
           float size, juce::Colour,
           juce::Justification = juce::Justification::centred, bool bold = true);

/** Letter-spaced run — the section titles and the wordmark. */
void tracked (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
              float size, juce::Colour, float tracking,
              bool leftAlign = true, bool bold = true);

void rule (juce::Graphics&, float x, float y, float w, float h, float alpha = 0.13f);

/* ── Controls ────────────────────────────────────────────────────────────── */

/** Fader. The handle hugs the travel — 6 pt track, 14 pt handle. Bipolar ones
    fill out from the middle, which is where their zero is. */
void fader (juce::Graphics&, float cx, float norm, juce::Colour,
            const juce::String& label, const juce::String& readout,
            bool fromCentre = false);

/** N-position selector; every position stays labelled. */
void selector (juce::Graphics&, juce::Rectangle<float>, int selected,
               juce::Colour, const juce::StringArray& labels, float size = 9.0f);

/** The same switch read top to bottom - one label a row, nothing abbreviated. */
void selectorVertical (juce::Graphics&, juce::Rectangle<float>, int selected,
                       juce::Colour, const juce::StringArray& labels, float size = 9.0f);

/** Latching or momentary button, lit while it is on. */
void button (juce::Graphics&, juce::Rectangle<float>, bool on,
             juce::Colour, const juce::String& label);

/**
 * The recording buffer: the write head sweeping it, and the window the grains
 * are read from. This is the one thing the module cannot show you, and the one
 * thing that makes freeze legible.
 */
void bufferStrip (juce::Graphics&, juce::Rectangle<float>, float head,
                  float position, float window, bool frozen, bool live,
                  juce::Colour);

void meter (juce::Graphics&, juce::Rectangle<float>, float level, juce::Colour);

/** Gain-reduction tell-tale: fills downward as the limiter pulls. */
void reductionBar (juce::Graphics&, juce::Rectangle<float>, float reduction, juce::Colour);

/** The module's own display: four bi-colour LEDs as a VU bar. */
void ledBar (juce::Graphics&, float x, float cy, float level, juce::Colour);

void lamp (juce::Graphics&, float cx, float cy, bool on, juce::Colour);

} // namespace cld::panel
