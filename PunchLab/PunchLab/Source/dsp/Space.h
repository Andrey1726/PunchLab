#pragma once

#include "ModuleSlot.h"

namespace pl
{
// ============================================================================
// Space — реверберация на сети задержек с обратной связью (FDN, 8 линий,
// матрица Хаусхолдера), входной диффузор из 4 all-pass фильтров, пре-дилей,
// частотно-зависимое затухание, модуляция линий, M/S-ширина.
// Типы: Room, Plate, Hall, Gated (гейтованный хвост 80-х), Ambience.
// ============================================================================
class Space : public EffectModule
{
public:
    void bind (juce::AudioProcessorValueTreeState& s) override
    {
        pType = s.getRawParameterValue (ID::spaceType);   pSize = s.getRawParameterValue (ID::spaceSize);
        pDecay = s.getRawParameterValue (ID::spaceDecay); pPre = s.getRawParameterValue (ID::spacePredelay);
        pDamp = s.getRawParameterValue (ID::spaceDamp);   pWidth = s.getRawParameterValue (ID::spaceWidth);
    }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        const int maxLine = (int) (0.25 * sr) + 64;
        for (auto& l : lines) l.prepare (maxLine);
        preDelay[0].prepare ((int) (0.25 * sr) + 8);
        preDelay[1].prepare ((int) (0.25 * sr) + 8);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                diff[c][i].buf.prepare ((int) (0.03 * sr) + 8);
        gateEnv.set (0.5f, 30.0f, sr);
        amountSm.prepare (sr, 30.0f);
        preSm.prepare (sr, 60.0f);
        reset();
    }

    void reset() override
    {
        for (auto& l : lines) { l.buf.reset(); l.lp = 0.0f; }
        for (auto& p : preDelay) p.reset();
        for (auto& c : diff) for (auto& d : c) d.buf.reset();
        gateEnv.reset(); gateGain = 0.0f; gateHold = 0;
        lfoPhase = 0.0;
        amountSm.reset (0.0f);
        preSm.reset (pPre->load());
        hpL.reset(); hpR.reset();
    }

    void process (float* L, float* R, int n, float amount, const ProcessContext&) override
    {
        amountSm.setTarget (amount);
        preSm.setTarget (pPre->load());
        updateTopology();

        const float width = pWidth->load();
        const bool gated = type == 3;
        const float gateOpen = dbToGain (-40.0f);
        const float gateRelease = msToCoef (12.0f, sampleRate);
        const int holdSamples = (int) (juce::jlimit (0.05f, 1.0f, pDecay->load() * 0.25f) * sampleRate);
        const float lfoInc = (float) (0.37 / sampleRate);

        for (int i = 0; i < n; ++i)
        {
            const float a = amountSm.next();
            const float preSamples = preSm.next() * 0.001f * (float) sampleRate;

            // --- пре-дилей + диффузия ---
            preDelay[0].push (L[i]); preDelay[1].push (R[i]);
            float inL = preDelay[0].readDelay (preSamples);
            float inR = preDelay[1].readDelay (preSamples);
            inL = hpL.hp (inL); inR = hpR.hp (inR);
            for (int k = 0; k < 4; ++k) { inL = diff[0][k].process (inL, diffG); inR = diff[1][k].process (inR, diffG); }

            // --- FDN ---
            lfoPhase += lfoInc; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            const float lfo = std::sin ((float) lfoPhase * kTwoPi);
            float y[kLines]; float sum = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                float d = lineLen[k];
                if (k == 1) d += modDepth * lfo;
                if (k == 4) d -= modDepth * lfo;
                float v = lines[k].buf.readDelay (d);
                v = lines[k].lp = v + dampA * (lines[k].lp - v); // НЧ-затухание
                v *= lineGain[k];
                y[k] = v; sum += v;
            }
            const float hh = sum * (2.0f / (float) kLines);
            for (int k = 0; k < kLines; ++k)
            {
                const float inj = (k & 1) ? inR : inL;
                lines[k].buf.push (y[k] - hh + inj * 0.5f);
            }

            float wl = (y[0] - y[2] + y[4] - y[6] + y[1] * 0.5f - y[5] * 0.5f) * 0.42f;
            float wr = (y[1] - y[3] + y[5] - y[7] + y[2] * 0.5f - y[6] * 0.5f) * 0.42f;

            // --- Gated: открывается по входу, держит hold, быстро закрывается ---
            if (gated)
            {
                const float e = gateEnv.process (std::max (std::abs (L[i]), std::abs (R[i])));
                if (e > gateOpen) gateHold = holdSamples;
                float target = gateHold > 0 ? 1.0f : 0.0f;
                if (gateHold > 0) --gateHold;
                gateGain = target > gateGain ? target + 0.2f * (gateGain - target) : target + gateRelease * (gateGain - target);
                wl *= gateGain * 1.4f; wr *= gateGain * 1.4f;
            }

            // --- ширина (M/S) ---
            const float m = 0.5f * (wl + wr), s = 0.5f * (wr - wl) * width;
            wl = m - s; wr = m + s;

            L[i] += a * wl;
            R[i] += a * wr;
        }
    }

private:
    static constexpr int kLines = 8;

    struct Line { RingBuffer buf; float lp = 0.0f; void prepare (int n) { buf.prepare (n); } };

    struct Allpass
    {
        RingBuffer buf; float len = 100.0f;
        float process (float x, float g)
        {
            const float d = buf.readDelay (len);
            const float v = x + g * d;
            buf.push (v);
            return d - g * v;
        }
    };

    void updateTopology()
    {
        const int   t     = (int) pType->load();
        const float size  = pSize->load();
        const float decay = pDecay->load();
        const float damp  = pDamp->load();
        if (t == type && size == lastSize && decay == lastDecay && damp == lastDamp) return;
        type = t; lastSize = size; lastDecay = decay; lastDamp = damp;

        static const float baseMs[kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.3f, 73.1f };
        static const float diffMs[4]      = { 4.77f, 3.59f, 12.73f, 9.31f };

        float scale = 1.0f, rt = decay, hiCut = 12000.0f, dg = 0.65f, hp = 60.0f;
        switch (t)
        {
            case 0: scale = 0.25f + 0.45f * size; dg = 0.62f; hiCut = 9000.0f;  break;  // Room
            case 1: scale = 0.45f + 0.55f * size; dg = 0.75f; hiCut = 16000.0f; break;  // Plate
            case 2: scale = 0.8f  + 1.2f  * size; dg = 0.68f; hiCut = 8000.0f;  rt = decay * 1.4f; break; // Hall
            case 3: scale = 0.35f + 0.4f  * size; dg = 0.7f;  hiCut = 11000.0f; rt = 3.0f; break; // Gated
            default:scale = 0.12f + 0.25f * size; dg = 0.72f; hiCut = 12000.0f; rt = std::min (decay, 0.8f); break; // Ambience
        }
        hiCut *= (1.0f - 0.8f * damp);
        diffG = dg;
        dampA = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * std::max (800.0f, hiCut) / sampleRate);
        modDepth = (float) (0.0006 * sampleRate * (t == 1 ? 0.5 : 1.0));

        for (int k = 0; k < kLines; ++k)
        {
            lineLen[k] = juce::jlimit (8.0f, (float) (0.24 * sampleRate), baseMs[k] * scale * 0.001f * (float) sampleRate);
            lineGain[k] = std::pow (10.0f, -3.0f * lineLen[k] / (std::max (0.05f, rt) * (float) sampleRate));
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                diff[c][i].len = diffMs[i] * (c ? 1.07f : 1.0f) * (0.6f + 0.6f * size) * 0.001f * (float) sampleRate;

        hpL.setCutoff (hp, sampleRate); hpR.setCutoff (hp, sampleRate);
    }

    double sampleRate = 44100.0, lfoPhase = 0.0;
    Line lines[kLines];
    RingBuffer preDelay[2];
    Allpass diff[2][4];
    float lineLen[kLines] {}, lineGain[kLines] {};
    float diffG = 0.65f, dampA = 0.5f, modDepth = 20.0f;
    int type = -1; float lastSize = -1, lastDecay = -1, lastDamp = -1;
    EnvFollower gateEnv; float gateGain = 0.0f; int gateHold = 0;
    OnePole hpL, hpR;
    Smoother amountSm, preSm;

    std::atomic<float>* pType = nullptr; std::atomic<float>* pSize = nullptr; std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pPre = nullptr; std::atomic<float>* pDamp = nullptr; std::atomic<float>* pWidth = nullptr;
};
} // namespace pl
