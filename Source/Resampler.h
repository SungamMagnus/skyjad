#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cld
{

/**
 * Kaiser-windowed sinc resampler, arbitrary ratio, push-driven.
 *
 * The module is a 32 kHz machine and nothing else, so every host block crosses
 * a rate boundary twice. A polynomial interpolator will not do it: at 44.1 or
 * 48 kHz the transition band is barely half an octave wide, and anything with a
 * gentle skirt folds the top of the spectrum straight into the grain buffer.
 *
 * The kernel table is shared and fixed; `scale_` alone sets the cut-off, which
 * lands at 0.45 x the lower of the two rates - about 14.4 kHz either way. That
 * is roughly where the module's own codec gives up, so the band limit is part
 * of the sound rather than a compromise.
 */
class SincResampler
{
public:
    void prepare (double inRate, double outRate, int numChannels, int maxInputBlock)
    {
        channels_ = numChannels;
        step_     = inRate / outRate;                  // input frames per output frame
        scale_    = std::min (1.0, outRate / inRate);  // kernel time-scale
        support_  = kHalfWidth / scale_;               // half-width, in input frames

        histSize_ = 1;
        while (histSize_ < (int) std::ceil (support_) * 2 + maxInputBlock * 2 + 256)
            histSize_ <<= 1;
        mask_ = histSize_ - 1;
        hist_.assign ((size_t) channels_ * (size_t) histSize_, 0.0f);

        taps_.assign ((size_t) std::ceil (support_) * 2 + 4, 0.0f);
        gain_ = normalisation();

        reset();
    }

    void reset()
    {
        std::fill (hist_.begin(), hist_.end(), 0.0f);
        written_ = 0;
        read_    = support_;   // first output sits where the kernel is fully fed
    }

    /** Group delay, in output frames. */
    double latencyOut() const { return support_ / step_; }

    /** Takes every one of numIn frames; emits as many as the ratio yields, capped. */
    int process (const float* const* in, int numIn, float* const* out, int maxOut)
    {
        for (int ch = 0; ch < channels_; ++ch)
        {
            float* h = &hist_[(size_t) ch * (size_t) histSize_];
            for (int i = 0; i < numIn; ++i)
                h[(int) ((written_ + i) & mask_)] = in[ch][i];
        }
        written_ += numIn;

        int produced = 0;
        while (produced < maxOut && read_ + support_ < (double) written_ - 1.0)
        {
            const int64_t i0 = (int64_t) std::ceil  (read_ - support_);
            const int64_t i1 = (int64_t) std::floor (read_ + support_);
            const int n = (int) (i1 - i0 + 1);

            for (int t = 0; t < n; ++t)
                taps_[(size_t) t] = kernel ((double) (i0 + t) - read_) * gain_;

            for (int ch = 0; ch < channels_; ++ch)
            {
                const float* h = &hist_[(size_t) ch * (size_t) histSize_];
                float sum = 0.0f;
                for (int t = 0; t < n; ++t)
                    sum += h[(int) ((i0 + t) & mask_)] * taps_[(size_t) t];
                out[ch][produced] = sum;
            }

            ++produced;
            read_ += step_;
        }

        /* Both cursors are absolute frame counts. Rebasing by a whole number of
           ring turns keeps the indices put and the mantissa short. */
        if (read_ > 1.0e9)
        {
            const double turns = (double) histSize_ * 4096.0;
            read_    -= turns;
            written_ -= (int64_t) turns;
        }
        return produced;
    }

private:
    static constexpr int    kHalfWidth = 32;    // kernel half-width, filter units
    static constexpr int    kRes       = 512;   // table entries per filter unit
    static constexpr double kBeta      = 8.5;   // Kaiser beta, ~ -80 dB stopband
    static constexpr double kRolloff   = 0.90;

    static const std::vector<float>& table()
    {
        static const std::vector<float> t = buildTable();
        return t;
    }

    static std::vector<float> buildTable()
    {
        constexpr double pi = 3.14159265358979323846;
        const double fc = 0.5 * kRolloff;

        std::vector<float> t ((size_t) (kHalfWidth * kRes + 2), 0.0f);
        for (size_t i = 0; i < t.size(); ++i)
        {
            const double u = (double) i / kRes;
            const double x = 2.0 * pi * fc * u;
            const double s = u < 1.0e-9 ? 1.0 : std::sin (x) / x;
            t[i] = (float) (2.0 * fc * s * kaiser (u / kHalfWidth));
        }
        return t;
    }

    static double kaiser (double t)
    {
        if (t >= 1.0) return 0.0;
        return besselI0 (kBeta * std::sqrt (1.0 - t * t)) / besselI0 (kBeta);
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x * 0.5) / k;
            sum  += term * term;
        }
        return sum;
    }

    inline float kernel (double x) const
    {
        const auto& t = table();
        const double u = std::abs (x) * scale_ * (double) kRes;
        const size_t i = (size_t) u;
        if (i + 1 >= t.size())
            return 0.0f;
        return t[i] + (t[i + 1] - t[i]) * (float) (u - (double) i);
    }

    /** Unity DC gain, averaged over the phase - the kernel sum drifts with it. */
    float normalisation() const
    {
        constexpr int kPhases = 64;
        double acc = 0.0;
        for (int p = 0; p < kPhases; ++p)
        {
            const double frac = (double) p / kPhases;
            double sum = 0.0;
            for (int64_t i = (int64_t) std::ceil (frac - support_);
                 i <= (int64_t) std::floor (frac + support_); ++i)
                sum += kernel ((double) i - frac);
            acc += sum;
        }
        return acc > 1.0e-9 ? (float) ((double) kPhases / acc) : 1.0f;
    }

    std::vector<float> hist_, taps_;
    int     channels_ = 2, histSize_ = 0, mask_ = 0;
    double  step_ = 1.0, scale_ = 1.0, support_ = 0.0, read_ = 0.0;
    int64_t written_ = 0;
    float   gain_ = 1.0f;
};

} // namespace cld
