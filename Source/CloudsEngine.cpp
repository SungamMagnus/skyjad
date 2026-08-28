#include "CloudsEngine.h"

#include <algorithm>
#include <cmath>

namespace cld
{

namespace
{
/* Silence pushed into the up-sampler ahead of the signal. It has to cover the
   32-frame block quantisation plus the sinc's own fill, or the host block runs
   dry every time the accumulator lands short. */
constexpr int kPrimeFrames = 80;

constexpr float kEnvCoeff = 0.02f;

/* Codec word in, codec word out, the firmware runs 9.4 dB down: the blend LUT
   tops out at 1/sqrt(2) and SoftConvert() halves again on the way to the DAC,
   so a fully dry setting leaves the DSP at 0.35. On a module fed and read in
   volts that hardly matters; in a host it does, because a plug-in that is not
   unity when it is fully dry is simply broken.
   Undoing exactly those two factors - after the conversion, so the internal
   clip point stays where the firmware put it - restores unity. It also puts
   the module's remaining headroom above 0 dBFS rather than below it: the wet
   path sits at its own +1.6 dB post-gain and can reach +9 dB before the
   firmware's soft limiter, which is where the module itself would be running
   hot. Drop this constant to 1.0 for the raw fixed-point staging. */
constexpr float kOutputTrim = 2.0f * 1.41421356f;

inline int16_t toCodec (float x)
{
    const float c = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
    return (int16_t) std::lrintf (c * 32767.0f);
}
} // namespace

CloudsEngine::CloudsEngine()
    : proc_ (std::make_unique<clouds::GranularProcessor>()),
      large_ (kLargeBufferBytes, 0),
      small_ (kSmallBufferBytes, 0)
{
    blockIn_.resize ((size_t) kBlockFrames);
    blockOut_.resize ((size_t) kBlockFrames);

    proc_->Init (large_.data(), large_.size(), small_.data(), small_.size());
    proc_->set_playback_mode ((clouds::PlaybackMode) mode_);
    proc_->set_quality (quality_);
    recomputeBufferGeometry();
}

CloudsEngine::~CloudsEngine() = default;

void CloudsEngine::prepare (double hostSampleRate, int maxBlockSize)
{
    hostRate_ = hostSampleRate > 0.0 ? hostSampleRate : 48000.0;
    const int maxBlock = std::max (32, maxBlockSize);

    down_.prepare (hostRate_, kModuleRate, 2, maxBlock);
    up_  .prepare (kModuleRate, hostRate_, 2, maxBlock);

    /* Worst case the down-sampler yields more 32 kHz frames than host frames
       (a session below 32 kHz), so the scratch has to allow for the ratio. */
    const int cap = (int) std::ceil ((double) maxBlock * kModuleRate / hostRate_) + 64;
    for (int ch = 0; ch < 2; ++ch)
    {
        d32_[ch].assign ((size_t) cap, 0.0f);
        q32_[ch].assign ((size_t) (cap + kBlockFrames * 2), 0.0f);
        pend_[ch].assign ((size_t) kBlockFrames, 0.0f);
    }

    calibrateLatency (maxBlock);
    reset();
}

void CloudsEngine::reset()
{
    down_.reset();
    up_.reset();

    pendCount_ = 0;
    qCount_    = 0;
    envIn_ = envOut_ = peakIn_ = 0.0f;

    for (int ch = 0; ch < 2; ++ch)
        std::fill (pend_[ch].begin(), pend_[ch].end(), 0.0f);

    /* Prime the up-sampler: written but never read, so it becomes pure delay.
       The (already zeroed) down-sample scratch stands in for the silence. */
    if ((int) d32_[0].size() >= kPrimeFrames)
    {
        std::fill (d32_[0].begin(), d32_[0].end(), 0.0f);
        const float* z[2]   = { d32_[0].data(), d32_[0].data() };
        float*       sink[2] = { d32_[1].data(), d32_[1].data() };
        up_.process (z, kPrimeFrames, sink, 0);
    }
}

/**
 * Two sinc kernels, a 32-frame accumulator and a priming cushion sit between
 * the host and the module, and the delay they add together is not worth
 * deriving on paper. Bypassing the granular core turns the whole chain into a
 * plain wire, so an impulse through it reports the figure the host needs.
 */
void CloudsEngine::calibrateLatency (int maxBlockSize)
{
    const int blk    = std::max (32, std::min (maxBlockSize, 256));
    const int strike = 4 * blk;                 // fire once the pipeline is warm
    const int total  = strike + 8 * blk + 4096;

    proc_->set_bypass (true);
    reset();

    std::vector<float> l ((size_t) blk), r ((size_t) blk);
    int peakAt = strike;
    float peak = 0.0f;

    for (int n = 0; n < total; n += blk)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        if (n <= strike && strike < n + blk)
            l[(size_t) (strike - n)] = r[(size_t) (strike - n)] = 1.0f;

        process (l.data(), r.data(), blk);

        for (int i = 0; i < blk; ++i)
            if (std::abs (l[(size_t) i]) > peak)
            {
                peak = std::abs (l[(size_t) i]);
                peakAt = n + i;
            }
    }

    proc_->set_bypass (false);
    latency_ = std::max (0, peakAt - strike);
}

void CloudsEngine::setMode (int mode)
{
    mode = std::clamp (mode, 0, (int) numModes - 1);
    if (mode == mode_)
        return;

    mode_ = mode;
    proc_->set_playback_mode ((clouds::PlaybackMode) mode_);
    writeHead_ = 0;
}

void CloudsEngine::setQuality (int quality)
{
    quality = std::clamp (quality, 0, (int) numQualities - 1);
    if (quality == quality_)
        return;

    quality_ = quality;
    proc_->set_quality (quality_);
    recomputeBufferGeometry();
    writeHead_ = 0;
}

void CloudsEngine::recomputeBufferGeometry()
{
    const bool mono  = (quality_ & 1) != 0;
    const bool lofi  = (quality_ & 2) != 0;
    const size_t bytes = mono ? kLargeBufferBytes : kSmallBufferBytes;
    bufferFrames_ = (int) (lofi ? bytes : bytes >> 1);
    writeHead_ = std::min (writeHead_, bufferFrames_ - 1);
}

void CloudsEngine::runBlock()
{
    auto* p = proc_->mutable_parameters();
    p->position      = pending_.position;
    p->size          = pending_.size;
    p->pitch         = pending_.pitch;
    p->density       = pending_.density;
    p->texture       = pending_.texture;
    p->dry_wet       = pending_.dryWet;
    p->stereo_spread = pending_.spread;
    p->feedback      = pending_.feedback;
    p->reverb        = pending_.reverb;
    p->freeze        = pending_.freeze;
    p->trigger       = triggerArmed_;
    p->gate          = gate_;
    triggerArmed_ = false;

    for (int i = 0; i < kBlockFrames; ++i)
    {
        blockIn_[(size_t) i].l = toCodec (pend_[0][(size_t) i]);
        blockIn_[(size_t) i].r = toCodec (pend_[1][(size_t) i]);
    }

    /* The firmware runs Prepare() in its main loop while Process() fires from
       the codec interrupt. Here there is one thread, so it runs block by block:
       it is where buffers get rebuilt and where the vocoder and the WSOLA
       correlator do their between-block work. */
    proc_->Prepare();
    proc_->Process (blockIn_.data(), blockOut_.data(), (size_t) kBlockFrames);

    for (int i = 0; i < kBlockFrames; ++i)
    {
        q32_[0][(size_t) (qCount_ + i)] = (float) blockOut_[(size_t) i].l * (kOutputTrim / 32768.0f);
        q32_[1][(size_t) (qCount_ + i)] = (float) blockOut_[(size_t) i].r * (kOutputTrim / 32768.0f);
    }
    qCount_ += kBlockFrames;

    if (! pending_.freeze && mode_ != modeSpectral && bufferFrames_ > 0)
    {
        const int step = (quality_ & 2) ? kBlockFrames / 2 : kBlockFrames;
        writeHead_ = (writeHead_ + step) % bufferFrames_;
    }
}

void CloudsEngine::process (float* left, float* right, int numSamples)
{
    if (numSamples <= 0)
        return;

    const float* in[2]  = { left, right };
    float*       d32[2] = { d32_[0].data(), d32_[1].data() };

    const int n32 = down_.process (in, numSamples, d32, (int) d32_[0].size());

    qCount_ = 0;
    for (int i = 0; i < n32; ++i)
    {
        pend_[0][(size_t) pendCount_] = d32[0][i];
        pend_[1][(size_t) pendCount_] = d32[1][i];

        if (++pendCount_ == kBlockFrames)
        {
            runBlock();
            pendCount_ = 0;
        }
    }

    const float* q[2]   = { q32_[0].data(), q32_[1].data() };
    float*       out[2] = { left, right };
    const int produced  = up_.process (q, qCount_, out, numSamples);

    /* Only reachable if the priming was exhausted; holding the last frame lets
       the queue refill without shifting the stream. */
    for (int i = produced; i < numSamples; ++i)
    {
        left[i]  = produced > 0 ? left[produced - 1]  : 0.0f;
        right[i] = produced > 0 ? right[produced - 1] : 0.0f;
    }

    float peak = 0.0f, sumIn = 0.0f, sumOut = 0.0f;
    for (int i = 0; i < n32; ++i)
    {
        const float a = std::max (std::abs (d32[0][i]), std::abs (d32[1][i]));
        peak = std::max (peak, a);
        sumIn += a;
    }
    for (int i = 0; i < qCount_; ++i)
        sumOut += std::max (std::abs (q32_[0][(size_t) i]), std::abs (q32_[1][(size_t) i]));

    if (n32 > 0)
        envIn_ += kEnvCoeff * (sumIn / (float) n32 - envIn_);
    if (qCount_ > 0)
        envOut_ += kEnvCoeff * (sumOut / (float) qCount_ - envOut_);

    peakIn_ = std::max (peak, peakIn_ - 0.02f);
}

} // namespace cld
