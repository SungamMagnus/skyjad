#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace cld
{

/** Five knobs on the module's left half - the sound-forming controls. */
static constexpr int numGrain = 5;
/** The four values the module hides behind one shared Blend knob. */
static constexpr int numBlend = 4;

namespace pid
{
extern const juce::String grain[numGrain];   // position, size, pitch, density, texture
extern const juce::String blend[numBlend];   // drywet, spread, feedback, reverb
extern const juce::String mode;
extern const juce::String quality;
extern const juce::String freeze;
extern const juce::String noteTrack;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/* ── Mode and quality copy ───────────────────────────────────────────────── */

extern const char* const modeName[4];        // GRANULAR / STRETCH / DELAY / SPECTRAL
extern const char* const modeCaption[4];     // one line, what the engine does
extern const char* const qualityName[4];     // 1s ST / 2s MO / 4s ST / 8s MO
extern const char* const qualityCaption[4];  // one line, the trade

/* ── Panel readouts ──────────────────────────────────────────────────────
 * Compact strings for the faceplate. The host keeps the verbose forms that
 * createLayout() installs. */

/** Grain size in milliseconds: the LUT doubles every quarter turn. */
float grainSizeMs (float size);

/** Semitones for a pitch-pot position, straight off the firmware's own table.
    The taper is steep at the ends and almost flat around noon - a quarter turn
    either side of unity is only three semitones - and it leans on each
    semitone without ever locking to it. Passing the pot position rather than a
    semitone count is what keeps that feel. */
float pitchSemitones (float potPosition);

/** A grain control at its parameter value. Mode only ever changes the units,
    never what the control does. */
juce::String grainReadout (int idx, float value, int mode);

juce::String blendReadout (int idx, float value);

} // namespace cld
