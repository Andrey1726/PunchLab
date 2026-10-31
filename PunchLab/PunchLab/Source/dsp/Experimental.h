#pragma once

#include "ModuleSlot.h"

namespace pl
{
// ============================================================================
// Experimental — гранулярные и глитч-эффекты, синхронизированные с темпом хоста.
//   Granular  — облако гранул из недавнего прошлого (Size = длина гранул,
//               Chaos = разброс позиции/высоты/панорамы, Pitch = транспозиция)
//   Stutter   — повтор фрагмента в каждом шаге сетки (Size = длина фрагмента)
//   Reverse   — проигрывание предыдущего шага задом наперёд (Size = доля шага)
//   Tape Stop — «остановка ленты» в конце шага (Size = длительность торможения)
//   Scatter   — перестановка шагов из прошлого такта, случайный реверс
//   Ring Mod  — кольцевая модуляция (Size = частота несущей, Chaos = скачки)
// Chaos во всех шаговых режимах — вероятность «пропустить» шаг / рандомизация.
// ============================================================================
class Experimental : public EffectModule
{
public:
    void bind (juce::AudioProcessorValueTreeState& s) override
    {
        pMode = s.getRawParameterValue (ID::expMode);   pDiv = s.getRawParameterValue (ID::expDiv);
        pSize = s.getRawParameterValue (ID::expSize);   pChaos = s.getRawParameterValue (ID::expChaos);
        pPitch = s.getRawParameterValue (ID::expPitch);
    }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        const int len = (int) (sr * 10.0);
        buf[0].prepare (len); buf[1].prepare (len);
        amountSm.prepare (sr, 30.0f);
        ringFreqSm.prepare (sr, 20.0f);
        fadeLen = (float) (0.004 * sr);
        reset();
    }

    void reset() override
    {
        buf[0].reset(); buf[1].reset();
        written = 0; freeBeat = 0.0; lastStep = -1;
        for (auto& g : grains) g.active = false;
        spawnCounter = 0.0f; ringPhase = 0.0; ringRand = 1.0f;
        stepActive = false; stepRate = 1.0; stepSource = 0.0;
        amountSm.reset (0.0f);
        ringFreqSm.reset (100.0f);
    }

    void process (float* L, float* R, int n, float amount, const ProcessContext& ctx) override
    {
        amountSm.setTarget (amount);
        const int   mode   = (int) pMode->load();
        const double div   = expDivisionInBeats ((int) pDiv->load());
        const float size   = pSize->load();
        const float chaos  = pChaos->load();
        const float pitch  = pPitch->load();
        const double rate  = std::pow (2.0, pitch / 12.0);
        const double bpm   = juce::jlimit (30.0, 300.0, ctx.bpm);
        const double beatsPerSample = bpm / 60.0 / sampleRate;
        const double stepLen = div / beatsPerSample;

        if (mode != lastMode) { lastMode = mode; for (auto& g : grains) g.active = false; stepActive = false; }

        for (int i = 0; i < n; ++i)
        {
            const float a = amountSm.next();
            const float inL = L[i], inR = R[i];
            buf[0].push (inL); buf[1].push (inR);
            ++written;
            const double live = (double) (written - 1);

            // ---- сетка ----
            double beat;
            if (ctx.playing) beat = ctx.ppq + (double) i * beatsPerSample;
            else           { freeBeat += beatsPerSample; beat = freeBeat; }
            const long long step = (long long) std::floor (beat / div);
            const double t = (beat - (double) step * div) / beatsPerSample; // сэмплов от начала шага
            if (step != lastStep)
            {
                lastStep = step;
                onStep (mode, live - t, stepLen, size, chaos, rate);
            }

            if (a <= 0.0f) continue;

            float wl = inL, wr = inR;
            switch (mode)
            {
                case 0: granular (wl, wr, live, size, chaos, rate); break;
                case 1: stutter  (wl, wr, inL, inR, t, stepLen); break;
                case 2: reverse  (wl, wr, inL, inR, t, stepLen, size); break;
                case 3: tapeStop (wl, wr, inL, inR, t, stepLen, size, live); break;
                case 4: scatter  (wl, wr, inL, inR, t, stepLen); break;
                default:ringMod  (wl, wr, inL, inR, size, (float) rate); break;
            }
            L[i] = inL + a * (wl - inL);
            R[i] = inR + a * (wr - inR);
        }
    }

private:
    // ------------------------------------------------------------------ шаги
    void onStep (int mode, double boundaryAbs, double stepLen, float size, float chaos, double rate)
    {
        boundary = boundaryAbs;
        const bool skip = rng.uni() < chaos * 0.5f;
        stepActive = ! skip;
        stepRate = rate;

        switch (mode)
        {
            case 1: // Stutter: повторяем только что прозвучавший фрагмент
            {
                float s = size;
                if (chaos > 0.0f) s = juce::jlimit (0.0f, 1.0f, s + (rng.uni() - 0.5f) * chaos);
                const double frac = std::pow (2.0, -std::round (4.0 * (1.0 - s)));  // 1/16 … 1 шага
                sliceLen = std::max (64.0, stepLen * frac);
                stepSource = boundary - sliceLen * std::max (1.0, rate) - 1.0;
                break;
            }
            case 2: // Reverse: предыдущий шаг задом наперёд
                stepSource = boundary - 1.0;
                break;
            case 3: // Tape Stop
                tapeHead = boundary;
                break;
            case 4: // Scatter: случайный шаг из прошлого (минимум на ceil(rate) шагов назад)
            {
                const int minBack = (int) std::ceil (std::max (1.0, rate));
                const int maxBack = minBack + (int) (size * 7.0f);
                const int back = minBack + (int) (rng.uni() * (float) (maxBack - minBack + 1));
                stepActive = rng.uni() < 0.4f + 0.6f * chaos;
                stepSource = boundary - stepLen * back;
                stepRate = (rng.uni() < chaos * 0.35f) ? -rate : rate;
                if (stepRate < 0.0) stepSource += stepLen * std::abs (stepRate) - 1.0;
                break;
            }
            case 5: // Ring: случайные скачки частоты
                ringRand = std::pow (2.0f, (rng.uni() - 0.5f) * 4.0f * chaos);
                stepActive = true;
                break;
            default: break;
        }
        if (mode == 0) stepActive = true;
    }

    float edgeEnv (double t, double len) const
    {
        return (float) juce::jlimit (0.0, 1.0, std::min (t / fadeLen, (len - t) / fadeLen));
    }

    // --------------------------------------------------------------- режимы
    void granular (float& wl, float& wr, double live, float size, float chaos, double rate)
    {
        const float grainLen = (float) ((0.012 + 0.2 * size * size) * sampleRate);
        spawnCounter -= 1.0f;
        if (spawnCounter <= 0.0f)
        {
            spawnCounter = grainLen / 3.0f * (1.0f + chaos * (rng.uni() - 0.5f));
            for (auto& g : grains)
            {
                if (g.active) continue;
                const double r = rate * std::pow (2.0, (rng.uni() - 0.5f) * chaos * 1.0);
                const double lookBack = grainLen * std::max (0.0, r - 1.0) + 0.02 * sampleRate
                                      + chaos * rng.uni() * 0.6 * sampleRate;
                g.pos = live - lookBack;
                g.rate = r;
                g.len = (int) grainLen;
                g.age = 0;
                const float pan = 0.5f + (rng.uni() - 0.5f) * chaos;
                g.gl = std::sqrt (1.0f - pan) * 1.41f; g.gr = std::sqrt (pan) * 1.41f;
                g.active = true;
                break;
            }
        }
        float sl = 0.0f, sr = 0.0f;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const float w = 0.5f - 0.5f * std::cos (kTwoPi * (float) g.age / (float) g.len);
            sl += buf[0].readAbs (g.pos) * w * g.gl;
            sr += buf[1].readAbs (g.pos) * w * g.gr;
            g.pos += g.rate;
            if (++g.age >= g.len) g.active = false;
        }
        wl = sl * 0.62f; wr = sr * 0.62f;
    }

    void stutter (float& wl, float& wr, float inL, float inR, double t, double stepLen)
    {
        if (! stepActive) return;
        const double loopT = std::fmod (t, sliceLen);
        const double pos = stepSource + loopT * stepRate;
        const float e = edgeEnv (t, stepLen) * edgeEnv (loopT, sliceLen);
        wl = inL + e * (buf[0].readAbs (pos) - inL);
        wr = inR + e * (buf[1].readAbs (pos) - inR);
    }

    void reverse (float& wl, float& wr, float inL, float inR, double t, double stepLen, float size)
    {
        if (! stepActive) return;
        const double revLen = stepLen * (0.25 + 0.75 * size);
        if (t >= revLen) return;
        const double pos = stepSource - t * stepRate;
        const float e = edgeEnv (t, revLen);
        wl = inL + e * (buf[0].readAbs (pos) - inL);
        wr = inR + e * (buf[1].readAbs (pos) - inR);
    }

    void tapeStop (float& wl, float& wr, float inL, float inR, double t, double stepLen, float size, double live)
    {
        if (! stepActive) return;
        const double stopLen = stepLen * (0.15 + 0.85 * size);
        const double stopStart = stepLen - stopLen;
        if (t < stopStart) { tapeHead = live; return; }
        const double prog = juce::jlimit (0.0, 1.0, (t - stopStart) / stopLen);
        const double r = (1.0 - prog) * (1.0 - prog);
        tapeHead = std::min (tapeHead + r, live);
        const float fadeOut = (float) std::min (1.0, (1.0 - prog) * 8.0);
        const float e = edgeEnv (t, stepLen);
        wl = inL + e * (buf[0].readAbs (tapeHead) * fadeOut - inL);
        wr = inR + e * (buf[1].readAbs (tapeHead) * fadeOut - inR);
    }

    void scatter (float& wl, float& wr, float inL, float inR, double t, double stepLen)
    {
        if (! stepActive) return;
        const double pos = stepSource + t * stepRate;
        const float e = edgeEnv (t, stepLen);
        wl = inL + e * (buf[0].readAbs (pos) - inL);
        wr = inR + e * (buf[1].readAbs (pos) - inR);
    }

    void ringMod (float& wl, float& wr, float inL, float inR, float size, float rate)
    {
        ringFreqSm.setTarget (30.0f * std::pow (2.0f, size * 6.0f) * rate * ringRand);
        ringPhase += (double) ringFreqSm.next() / sampleRate;
        ringPhase -= std::floor (ringPhase);
        const float c = std::sin ((float) ringPhase * kTwoPi);
        wl = inL * c * 1.3f; wr = inR * c * 1.3f;
    }

    struct Grain { double pos = 0, rate = 1; int len = 1, age = 0; float gl = 1, gr = 1; bool active = false; };

    double sampleRate = 44100.0;
    RingBuffer buf[2];
    long long written = 0, lastStep = -1;
    double freeBeat = 0.0, boundary = 0.0, stepSource = 0.0, stepRate = 1.0, sliceLen = 1000.0, tapeHead = 0.0, ringPhase = 0.0;
    bool stepActive = false;
    int lastMode = -1;
    float fadeLen = 100.0f, spawnCounter = 0.0f, ringRand = 1.0f;
    Grain grains[24];
    NoiseGen rng { 0x51ed270bu };
    Smoother amountSm, ringFreqSm;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pDiv = nullptr; std::atomic<float>* pSize = nullptr;
    std::atomic<float>* pChaos = nullptr; std::atomic<float>* pPitch = nullptr;
};
} // namespace pl
