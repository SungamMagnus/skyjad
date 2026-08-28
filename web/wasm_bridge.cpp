// WebAssembly entry points for the browser build.
//
// This is a thin shim, deliberately: the engine it drives is the same
// CloudsEngine the plug-in uses, sinc rate conversion and all, so the page and
// the VST run identical code down to the 16-bit codec word.
//
// Audio crosses the boundary through two fixed buffers rather than malloc, so
// the worklet can write, call process(), and read back without allocating on
// the audio thread.

#include "CloudsEngine.h"

#include "clouds/resources.h"
#include "stmlib/dsp/dsp.h"

#include <algorithm>
#include <cstddef>

namespace
{
constexpr int kMaxBlock = 2048;      // worklets render 128; this is headroom

cld::CloudsEngine  engine;
cld::EngineParams  params;
float bufL[kMaxBlock];
float bufR[kMaxBlock];
} // namespace

extern "C"
{

__attribute__((used)) void cloudius_init (float sampleRate)
{
    engine.prepare ((double) sampleRate, kMaxBlock);
}

__attribute__((used)) float* cloudius_buf_l() { return bufL; }
__attribute__((used)) float* cloudius_buf_r() { return bufR; }

/** Takes the pot position, not semitones: the taper is the firmware's own
    table, so the browser and the plug-in read the knob identically. */
__attribute__((used)) float cloudius_pitch_semitones (float potPosition)
{
    const float p = potPosition < 0.0f ? 0.0f : (potPosition > 1.0f ? 1.0f : potPosition);
    return stmlib::Interpolate (clouds::lut_quantized_pitch, p, 1024.0f);
}

__attribute__((used)) void cloudius_set_params (
    float position, float size, float pitchPot, float density, float texture,
    float dryWet, float spread, float feedback, float reverb, int freeze)
{
    params.position = position;
    params.size     = size;
    params.pitch    = cloudius_pitch_semitones (pitchPot);
    params.density  = density;
    params.texture  = texture;
    params.dryWet   = dryWet;
    params.spread   = spread;
    params.feedback = feedback;
    params.reverb   = reverb;
    params.freeze   = freeze != 0;
    engine.setParameters (params);
}

__attribute__((used)) void cloudius_set_mode    (int m) { engine.setMode (m); }
__attribute__((used)) void cloudius_set_quality (int q) { engine.setQuality (q); }
__attribute__((used)) void cloudius_trigger()           { engine.trigger(); }
__attribute__((used)) void cloudius_set_gate (int g)    { engine.setGate (g != 0); }

/** Processes in place: the worklet fills the buffers, calls this, reads back. */
__attribute__((used)) void cloudius_process (int n)
{
    engine.process (bufL, bufR, std::min (n, kMaxBlock));
}

__attribute__((used)) float cloudius_head()           { return engine.bufferPhase(); }
__attribute__((used)) float cloudius_buffer_seconds() { return engine.bufferSeconds(); }
__attribute__((used)) int   cloudius_latency()        { return engine.latencySamples(); }

} // extern "C"
