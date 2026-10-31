#pragma once

#include "DspUtils.h"
#include "../Params.h"

namespace pl
{
// ============================================================================
// Master Section: 3-полосный EQ (Low shelf 100 Гц, Mid peak, High shelf 6 кГц),
// Air (high shelf 14 кГц), транзиент-шейпер Attack/Sustain, мягкий клиппер.
// Все «отклонения от нейтрали» масштабируются Magnitude.
// ============================================================================
class MasterSection
{
public:
    void bind (juce::AudioProcessorValueTreeState& s)
    {
        pLow = s.getRawParameterValue (ID::mLow);   pMid = s.getRawParameterValue (ID::mMid);
        pMidF = s.getRawParameterValue (ID::mMidFreq); pHigh = s.getRawParameterValue (ID::mHigh);
        pAir = s.getRawParameterValue (ID::mAir);   pAtk = s.getRawParameterValue (ID::mAttack);
        pSus = s.getRawParameterValue (ID::mSustain); pClip = s.getRawParameterValue (ID::mClip);
    }

    void prepare (double sr)
    {
        sampleRate = sr;
        atkFast.set (0.5f, 50.0f, sr); atkSlow.set (20.0f, 50.0f, sr);
        susFast.set (1.0f, 25.0f, sr); susSlow.set (1.0f, 300.0f, sr);
        gainSm = msToCoef (2.0f, sr);
        clipSm.prepare (sr, 30.0f);
        last.fill (-1000.0f);
        reset();
    }

    void reset()
    {
        for (auto& c : eq) for (auto& b : c) b.reset();
        atkFast.reset(); atkSlow.reset(); susFast.reset(); susSlow.reset();
        shGain = 1.0f;
        clipSm.reset (0.0f);
    }

    void process (float* L, float* R, int n, float magnitude)
    {
        const float m = magnitude;
        const std::array<float, 5> cur { pLow->load() * m, pMid->load() * m, pMidF->load(), pHigh->load() * m, pAir->load() * m };
        if (cur != last)
        {
            last = cur;
            for (int c = 0; c < 2; ++c)
            {
                eq[c][0].setLowShelf  (sampleRate, 100.0, 0.7, cur[0]);
                eq[c][1].setPeak      (sampleRate, cur[2], 0.8, cur[1]);
                eq[c][2].setHighShelf (sampleRate, 6000.0, 0.7, cur[3]);
                eq[c][3].setHighShelf (sampleRate, std::min (14000.0, sampleRate * 0.42), 0.6, cur[4]);
            }
        }
        const bool eqOn = cur[0] != 0.0f || cur[1] != 0.0f || cur[3] != 0.0f || cur[4] != 0.0f;

        const float attack = pAtk->load() * m, sustain = pSus->load() * m;
        const bool shaperOn = attack != 0.0f || sustain != 0.0f || shGain != 1.0f;
        clipSm.setTarget (pClip->load() * m);

        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (eqOn)
                for (int k = 0; k < 4; ++k) { l = eq[0][k].process (l); r = eq[1][k].process (r); }

            if (shaperOn)
            {
                const float det = std::max (std::abs (l), std::abs (r)) + 1.0e-6f;
                const float aF = atkFast.process (det), aS = atkSlow.process (det);
                const float sF = susFast.process (det), sS = susSlow.process (det);
                const float tr  = juce::jlimit (0.0f, 18.0f, gainToDb (aF / (aS + 1.0e-6f)));  // > 0 на атаке
                const float sus = juce::jlimit (0.0f, 18.0f, gainToDb (sS / (sF + 1.0e-6f)));  // > 0 на хвосте
                const float gDb = juce::jlimit (-12.0f, 12.0f, attack * tr * 0.6f + sustain * sus * 0.7f);
                const float g = dbToGain (gDb);
                shGain = g + gainSm * (shGain - g);
                l *= shGain; r *= shGain;
            }

            const float clip = clipSm.next();
            if (clip > 0.0f)
            {
                const float drive = dbToGain (clip * 12.0f);
                const float cl = softClip (l * drive) / drive, cr = softClip (r * drive) / drive;
                const float makeup = 1.0f + clip * 0.6f;
                l = cl * makeup; r = cr * makeup;
                l = juce::jlimit (-1.0f, 1.0f, l); r = juce::jlimit (-1.0f, 1.0f, r);
            }
            L[i] = l; R[i] = r;
        }
    }

private:
    double sampleRate = 44100.0;
    Biquad eq[2][4];
    std::array<float, 5> last {};
    EnvFollower atkFast, atkSlow, susFast, susSlow;
    float shGain = 1.0f, gainSm = 0.0f;
    Smoother clipSm;
    std::atomic<float>* pLow = nullptr; std::atomic<float>* pMid = nullptr; std::atomic<float>* pMidF = nullptr;
    std::atomic<float>* pHigh = nullptr; std::atomic<float>* pAir = nullptr; std::atomic<float>* pAtk = nullptr;
    std::atomic<float>* pSus = nullptr; std::atomic<float>* pClip = nullptr;
};

// ============================================================================
// Loudness Match: K-взвешивание (ITU-R BS.1770: shelf +4 дБ @1.68 кГц + HPF 38 Гц),
// скользящая средняя мощность входа и выхода (~1.5 с), компенсирующий гейн
// с плавным изменением. В тишине усиление «замораживается».
// ============================================================================
class LoudnessMatch
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto* set : { &kIn, &kOut })
            for (auto& ch : *set)
            {
                ch[0].setHighShelf (sr, 1681.97, 0.7072, 3.9998);
                ch[1].setHighPass (sr, 38.135, 0.5003);
            }
        powCoef = msToCoef (1500.0f, sr);
        gainCoef = msToCoef (250.0f, sr);
        reset();
    }
    void reset()
    {
        for (auto* set : { &kIn, &kOut }) for (auto& ch : *set) for (auto& b : ch) b.reset();
        pIn = pOut = 1.0e-9f; gain = 1.0f;
    }

    // Вызывается для блока: in — исходный сигнал, io — обработанный (in-place усиление)
    void process (const float* inL, const float* inR, float* L, float* R, int n, bool enabled)
    {
        for (int i = 0; i < n; ++i)
        {
            const float a = kIn[0][1].process (kIn[0][0].process (inL[i]));
            const float b = kIn[1][1].process (kIn[1][0].process (inR[i]));
            const float c = kOut[0][1].process (kOut[0][0].process (L[i]));
            const float d = kOut[1][1].process (kOut[1][0].process (R[i]));
            const float ein = a * a + b * b, eout = c * c + d * d;
            pIn  = ein  + powCoef * (pIn  - ein);
            pOut = eout + powCoef * (pOut - eout);

            float target = gain;
            if (pIn > 1.0e-8f && pOut > 1.0e-8f)       // ~ -80 дБ: ниже — «замораживаем»
                target = juce::jlimit (0.063f, 4.0f, std::sqrt (pIn / pOut));
            if (! enabled) target = 1.0f;
            gain = target + gainCoef * (gain - target);
            L[i] *= gain; R[i] *= gain;
        }
        currentGainDb.store (gainToDb (gain));
    }

    std::atomic<float> currentGainDb { 0.0f };

private:
    double sampleRate = 44100.0;
    Biquad kIn[2][2], kOut[2][2];
    float pIn = 1.0e-9f, pOut = 1.0e-9f, gain = 1.0f, powCoef = 0.0f, gainCoef = 0.0f;
};
} // namespace pl
