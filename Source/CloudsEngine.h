#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "Resampler.h"

#include "clouds/dsp/granular_processor.h"

namespace cld
{

/** The module's own rate. Everything inside runs here and nowhere else. */
constexpr double kModuleRate  = 32000.0;
constexpr int    kBlockFrames = (int) clouds::kMaxBlockSize;   // 32

/** Sample memory, byte for byte as the firmware declares it, so grain and loop
    lengths come out identical to the hardware's. */
constexpr size_t kLargeBufferBytes = 118784;
constexpr size_t kSmallBufferBytes = 65536 - 128;

enum Mode  { modeGranular = 0, modeStretch, modeDelay, modeSpectral, numModes };
enum Qual  { qualStereoHi = 0, qualMonoHi, qualStereoLo, qualMonoLo, numQualities };

/** One control-rate snapshot, in the module's own units. */
struct EngineParams
{
    float position = 0.0f;
    float size     = 0.5f;
    float pitch    = 0.0f;     // semitones, -48 .. +48
    float density  = 0.5f;
    float texture  = 0.5f;
    float dryWet   = 0.5f;
    float spread   = 0.0f;
    float feedback = 0.0f;
    float reverb   = 0.0f;
    bool  freeze   = false;
};

/**
 * Host-rate wrapper around the firmware's GranularProcessor.
 *
 * Three things stand between a host block and the module: rate conversion to
 * 32 kHz and back, the fixed 32-frame block the DSP expects, and the 16-bit
 * codec word. All three are part of what Clouds sounds like, so none of them
 * are optimised away - the float block is converted, clipped and quantised
 * exactly as the codec would.
 */
class CloudsEngine
{
public:
    CloudsEngine();
    ~CloudsEngine();

    void prepare (double hostSampleRate, int maxBlockSize);
    void reset();

    /** Mode and quality rebuild the sample memory, so they are only pushed
        through on an actual change. */
    void setMode (int mode);
    void setQuality (int quality);
    void setParameters (const EngineParams& p) { pending_ = p; }

    /** One-shot: fires a grain, or re-syncs the loop. */
    void trigger() { triggerArmed_ = true; }
    void setGate (bool g) { gate_ = g; }

    void process (float* left, float* right, int numSamples);

    int   latencySamples() const { return latency_; }
    float inLevel()   const { return envIn_; }
    float outLevel()  const { return envOut_; }
    float inPeak()    const { return peakIn_; }

    /** Write head, 0..1 around the recording buffer. */
    float bufferPhase()   const { return bufferFrames_ > 0 ? (float) writeHead_ / (float) bufferFrames_ : 0.0f; }
    float bufferSeconds() const { return (float) ((double) bufferFrames_ / internalRate()); }
    double internalRate() const { return kModuleRate / (quality_ & 2 ? 2.0 : 1.0); }

private:
    void runBlock();
    void recomputeBufferGeometry();
    void calibrateLatency (int maxBlockSize);

    std::unique_ptr<clouds::GranularProcessor> proc_;
    std::vector<uint8_t> large_, small_;

    SincResampler down_, up_;
    std::vector<float> d32_[2], q32_[2], pend_[2];
    int  pendCount_ = 0, qCount_ = 0;

    std::vector<clouds::ShortFrame> blockIn_, blockOut_;

    EngineParams pending_ {};
    int  mode_ = modeGranular, quality_ = qualStereoHi;
    bool triggerArmed_ = false, gate_ = false;

    double hostRate_ = 48000.0;
    int    latency_  = 0;
    int    bufferFrames_ = 0, writeHead_ = 0;

    float envIn_ = 0.0f, envOut_ = 0.0f, peakIn_ = 0.0f;
};

} // namespace cld
