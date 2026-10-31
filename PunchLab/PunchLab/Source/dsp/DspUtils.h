#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>
#include <atomic>

namespace pl
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain (float db)        { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)         { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

// Коэффициент экспоненциального сглаживания для заданного времени (мс)
inline float msToCoef (float ms, double sr)
{
    if (ms <= 0.0f) return 0.0f;
    return (float) std::exp (-1.0 / (0.001 * (double) ms * sr));
}

inline float softClip (float x)  // быстрый tanh-подобный клиппер
{
    if (x >  3.0f) return  1.0f;
    if (x < -3.0f) return -1.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// --------------------------------------------------------------------------
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float fc, double sr)  { a = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * fc / sr); }
    float lp (float x)                    { z = x + a * (z - x); return z; }
    float hp (float x)                    { return x - lp (x); }
    void reset()                          { z = 0.0f; }
};

struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.995f;
    void prepare (double sr)    { r = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 10.0 / sr); }
    float process (float x)     { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
    void reset()                { x1 = y1 = 0.0f; }
};

// Пиковый детектор огибающей (attack/release)
struct EnvFollower
{
    float atk = 0.0f, rel = 0.0f, env = 0.0f;
    void set (float atkMs, float relMs, double sr) { atk = msToCoef (atkMs, sr); rel = msToCoef (relMs, sr); }
    float process (float x)
    {
        const float c = x > env ? atk : rel;
        env = x + c * (env - x);
        return env;
    }
    void reset() { env = 0.0f; }
};

// Линейное сглаживание параметра в пределах блока
struct Smoother
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int   remaining = 0, rampLen = 1;
    void prepare (double sr, float ms)   { rampLen = std::max (1, (int) (sr * ms * 0.001)); }
    void reset (float v)                 { current = target = v; remaining = 0; step = 0.0f; }
    void setTarget (float v)
    {
        if (v == target) return;
        target = v;
        remaining = rampLen;
        step = (target - current) / (float) rampLen;
    }
    float next()
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
    bool isSmoothing() const { return remaining > 0; }
};

// Целочисленная стерео-задержка (для компенсации латентности)
class StereoDelay
{
public:
    void prepare (int delaySamples, int maxBlock)
    {
        delay = std::max (0, delaySamples);
        size  = delay + maxBlock + 1;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        w = 0;
    }
    void reset()            { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); w = 0; }
    int  getDelay() const   { return delay; }

    void process (float* L, float* R, int n)
    {
        if (delay == 0) return;
        for (int i = 0; i < n; ++i)
        {
            int r = w - delay; if (r < 0) r += size;
            buf[0][(size_t) w] = L[i]; buf[1][(size_t) w] = R[i];
            L[i] = buf[0][(size_t) r]; R[i] = buf[1][(size_t) r];
            if (++w >= size) w = 0;
        }
    }
private:
    std::vector<float> buf[2];
    int size = 1, w = 0, delay = 0;
};

// Простой циклический буфер с дробным чтением
class RingBuffer
{
public:
    void prepare (int samples)  { data.assign ((size_t) std::max (4, samples), 0.0f); w = 0; }
    void reset()                { std::fill (data.begin(), data.end(), 0.0f); w = 0; }
    void push (float x)         { data[(size_t) w] = x; if (++w >= (int) data.size()) w = 0; }
    int  size() const           { return (int) data.size(); }
    int  writePos() const       { return w; }

    // задержка в сэмплах назад от последнего записанного
    float readDelay (float delaySamples) const
    {
        const int sz = (int) data.size();
        float pos = (float) w - 1.0f - delaySamples;
        while (pos < 0.0f) pos += (float) sz;
        return readAbs (pos);
    }
    // абсолютная позиция (может быть дробной)
    float readAbs (double pos) const
    {
        const int sz = (int) data.size();
        double p = std::fmod (pos, (double) sz); if (p < 0.0) p += sz;
        const int i0 = (int) p;
        const int i1 = (i0 + 1) % sz;
        const float f = (float) (p - i0);
        return data[(size_t) i0] + f * (data[(size_t) i1] - data[(size_t) i0]);
    }
private:
    std::vector<float> data;
    int w = 0;
};

// Биквад (TDF-II), коэффициенты RBJ
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0.0f; }

    void setNormalised (double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
    void setPeak (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double al = std::sin (w) / (2.0 * q), c = std::cos (w), sA = 2.0 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA),
                       (A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA);
    }
    void setHighShelf (double sr, double f, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double al = std::sin (w) / (2.0 * q), c = std::cos (w), sA = 2.0 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
                       (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void setHighPass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setLowPass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setBandPass (double sr, double f, double q)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        setNormalised (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
};

// Аналитический сигнал (IIR-трансформатор Гильберта, коэффициенты O. Niemitalo)
class HilbertTransformer
{
public:
    void process (float x, float& re, float& im)
    {
        float o1 = x, o2 = x;
        for (int i = 0; i < 4; ++i) o1 = ap1[i].process (o1);
        for (int i = 0; i < 4; ++i) o2 = ap2[i].process (o2);
        re = delayed; delayed = o1;
        im = o2;
    }
    void reset() { for (auto& a : ap1) a.reset(); for (auto& a : ap2) a.reset(); delayed = 0.0f; }

    HilbertTransformer()
    {
        static const float c1[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static const float c2[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i) { ap1[i].k = c1[i] * c1[i]; ap2[i].k = c2[i] * c2[i]; }
    }
private:
    struct AP
    {
        float k = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float process (float x) { const float y = k * (x + y2) - x2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; }
        void reset() { x1 = x2 = y1 = y2 = 0.0f; }
    };
    AP ap1[4], ap2[4];
    float delayed = 0.0f;
};

// Быстрый генератор шума
struct NoiseGen
{
    juce::uint32 state = 0x12345678u;
    explicit NoiseGen (juce::uint32 seed = 0x12345678u) : state (seed) {}
    float next()  // [-1, 1)
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
    float uni() { return 0.5f * (next() + 1.0f); } // [0,1)
};

// Контекст обработки (данные хоста)
struct ProcessContext
{
    double sampleRate = 44100.0;
    double bpm = 120.0;
    double ppq = 0.0;        // позиция в четвертях на начало блока
    bool   playing = false;
};
} // namespace pl
