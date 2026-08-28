#include "Parameters.h"

#include <cmath>

#include "clouds/resources.h"
#include "stmlib/dsp/dsp.h"

namespace cld
{

namespace pid
{
const juce::String grain[numGrain] = { "position", "size", "pitch", "density", "texture" };
const juce::String blend[numBlend] = { "drywet", "spread", "feedback", "reverb" };
const juce::String mode    = "mode";
const juce::String quality = "quality";
const juce::String freeze  = "freeze";
const juce::String noteTrack = "notetrack";
}

const char* const modeName[4] = { "GRANULAR", "STRETCH", "DELAY", "SPECTRAL" };

const char* const modeCaption[4] = {
    "grain cloud - size and density scatter it",
    "wsola stretch - texture opens the filter",
    "looping delay - pitch shifts the loop",
    "phase vocoder - texture thins the spectrum"
};

const char* const qualityName[4] = { "1s ST", "2s MO", "4s ST", "8s MO" };

const char* const qualityCaption[4] = {
    "32 kHz stereo, 16-bit",
    "32 kHz mono, 16-bit",
    "16 kHz stereo, 8-bit u-law",
    "16 kHz mono, 8-bit u-law"
};

float grainSizeMs (float size)
{
    /* lut_grain_size: 1024 samples at 0, doubling every quarter turn, read at
       the module's 32 kHz. */
    const float samples = 1024.0f * std::pow (2.0f, 4.0f * juce::jlimit (0.0f, 1.0f, size));
    return samples * 1000.0f / 32000.0f;
}

float pitchSemitones (float potPosition)
{
    return stmlib::Interpolate (clouds::lut_quantized_pitch,
                                juce::jlimit (0.0f, 1.0f, potPosition), 1024.0f);
}

namespace
{
using Range  = juce::NormalisableRange<float>;
using Attrib = juce::AudioParameterFloatAttributes;

std::unique_ptr<juce::AudioParameterFloat> makeFloat (const juce::String& id,
                                                      const juce::String& name,
                                                      Range range, float def,
                                                      std::function<juce::String (float, int)> fmt)
{
    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, 1 }, name, range, def,
        Attrib().withStringFromValueFunction (std::move (fmt)));
}

juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

/** DENSITY is bipolar around noon: regular below, random above, silent across
    the dead band the firmware leaves in the middle. */
juce::String densityText (float v)
{
    if (v >= 0.47f && v <= 0.53f) return "OFF";
    const float amount = (v > 0.5f ? (v - 0.53f) : (0.47f - v)) * 2.12f;
    return juce::String (v < 0.5f ? "REG " : "RND ")
         + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, amount) * 100.0f)) + "%";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    /* ── Grain — the five pots, in the module's own 0..1 except for pitch. ── */
    layout.add (makeFloat (pid::grain[0], "Position", Range (0.0f, 1.0f), 0.0f,
                           [] (float v, int) { return percent (v); }));

    layout.add (makeFloat (pid::grain[1], "Size", Range (0.0f, 1.0f), 0.5f,
                           [] (float v, int)
                           { return juce::String (grainSizeMs (v), 0) + " ms"; }));

    /* Pitch is the pot's position, not a semitone count: the module's taper is
       most of the sweep spent within a few semitones of unity, and flattening
       it into a linear -24..+24 would throw that away. The displayed value is
       still semitones. A tracked MIDI note adds on top, and the firmware
       constrains the sum to four octaves. */
    layout.add (makeFloat (pid::grain[2], "Pitch", Range (0.0f, 1.0f), 0.5f,
                           [] (float v, int)
                           { return juce::String (pitchSemitones (v), 2) + " st"; }));

    layout.add (makeFloat (pid::grain[3], "Density", Range (0.0f, 1.0f), 0.5f,
                           [] (float v, int) { return densityText (v); }));

    layout.add (makeFloat (pid::grain[4], "Texture", Range (0.0f, 1.0f), 0.5f,
                           [] (float v, int) { return percent (v); }));

    /* ── Blend — one control each, none of them sharing a knob. ─────────── */
    static const char* blendNames[numBlend] = { "Dry/Wet", "Stereo Spread", "Feedback", "Reverb" };
    static const float blendDefaults[numBlend] = { 0.5f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < numBlend; ++i)
        layout.add (makeFloat (pid::blend[i], blendNames[i], Range (0.0f, 1.0f), blendDefaults[i],
                               [] (float v, int) { return percent (v); }));

    /* ── Engine ──────────────────────────────────────────────────────────── */
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::mode, 1 }, "Mode",
        juce::StringArray { "Granular", "Stretch", "Looping Delay", "Spectral" }, 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::quality, 1 }, "Quality",
        juce::StringArray { "1s Stereo 16-bit", "2s Mono 16-bit",
                            "4s Stereo 8-bit", "8s Mono 8-bit" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { pid::freeze, 1 }, "Freeze", false));

    /* The module reads a V/Oct jack; there is no jack here, so the same job
       falls to the MIDI note - either it moves Pitch or it does not. */
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { pid::noteTrack, 1 }, "Note Tracking",
        juce::StringArray { "Off", "Track" }, 0));

    return layout;
}

juce::String grainReadout (int idx, float value, int mode)
{
    switch (idx)
    {
        case 0: return percent (value);
        case 1: return mode == 0 ? juce::String (juce::roundToInt (grainSizeMs (value))) + "ms"
                                 : percent (value);
        case 2: return juce::String (pitchSemitones (value), 1) + " st";
        case 3: return mode == 0 ? densityText (value) : percent (value);
        default: return percent (value);
    }
}

juce::String blendReadout (int, float value) { return percent (value); }

} // namespace cld
