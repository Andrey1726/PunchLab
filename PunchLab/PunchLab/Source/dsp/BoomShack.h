#pragma once

#include "ModuleSlot.h"

namespace pl
{
// ============================================================================
// Boom Shack
//  * Boom  — детектор низкочастотных ударов (НЧ-фильтр 150 Гц) запускает
//            синусоидальный генератор с «питч-свипом» (как у 808-бочки).
//  * Noise — детектор средне-высоких ударов (ВЧ-фильтр 1 кГц) запускает
//            всплеск полосового шума (корпус малого барабана, «сизл» тарелок).
//  Громкость слоёв повторяет силу удара (velocity из огибающей).
// ============================================================================
class BoomShack : public EffectModule
{
public:
    void bind (juce::AudioProcessorValueTreeState& s) override
    {
        pSub = s.getRawParameterValue (ID::boomSub);     pTune = s.getRawParameterValue (ID::boomTune);
        pDecay = s.getRawParameterValue (ID::boomDecay); pNoise = s.getRawParameterValue (ID::boomNoise);
        pTone = s.getRawParameterValue (ID::boomTone);   pNDecay = s.getRawParameterValue (ID::boomNDecay);
        pSens = s.getRawParameterValue (ID::boomSens);
    }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        lowDet.setCutoff (150.0f, sr);
        highDet.setCutoff (1000.0f, sr);
        boomTrig.prepare (sr); noiseTrig.prepare (sr);
        amountSm.prepare (sr, 30.0f);
        boomLp.setLowPass (sr, 400.0, 0.707);
        reset();
    }

    void reset() override
    {
        lowDet.reset(); highDet.reset(); boomTrig.reset(); noiseTrig.reset();
        boomEnv = noiseEnv = 0.0f; phase = 0.0; pitchEnv = 0.0f;
        for (auto& f : noiseBp) f.reset();
        for (auto& f : noiseHp) f.reset();
        boomLp.reset();
        amountSm.reset (0.0f);
    }

    void process (float* L, float* R, int n, float amount, const ProcessContext&) override
    {
        amountSm.setTarget (amount);

        const float sub = pSub->load(), noise = pNoise->load();
        const float tune = pTune->load();
        const float boomDecayCoef  = msToCoef (pDecay->load() / 6.9f, sampleRate);  // время спада до -60 дБ
        const float noiseDecayCoef = msToCoef (pNDecay->load() / 6.9f, sampleRate);
        const float pitchCoef = msToCoef (12.0f, sampleRate);
        const float sens = pSens->load();
        const float thr = dbToGain (-12.0f - 36.0f * sens);   // порог срабатывания

        const float tone = pTone->load();
        if (std::abs (tone - lastTone) > 1.0f)
        {
            lastTone = tone;
            for (auto& f : noiseBp) f.setBandPass (sampleRate, tone, 0.9);
            for (auto& f : noiseHp) f.setHighPass (sampleRate, std::max (200.0f, tone * 0.35f), 0.707);
        }

        for (int i = 0; i < n; ++i)
        {
            const float a = amountSm.next();
            const float mono = 0.5f * (L[i] + R[i]);

            // --- детекторы ---
            float vel;
            if (boomTrig.process (std::abs (lowDet.lp (mono)) * 2.0f, thr, vel))
            {
                boomEnv = std::max (boomEnv, juce::jlimit (0.15f, 1.0f, vel * 1.5f));
                pitchEnv = 1.0f;
                phase = 0.0;
            }
            if (noiseTrig.process (std::abs (highDet.hp (mono)) * 2.0f, thr, vel))
                noiseEnv = std::max (noiseEnv, juce::jlimit (0.15f, 1.0f, vel * 1.8f));

            // --- Boom: синус с питч-огибающей ---
            const float f = tune * (1.0f + 1.8f * pitchEnv);
            phase += (double) f / sampleRate;
            if (phase >= 1.0) phase -= 1.0;
            float boom = std::sin ((float) phase * kTwoPi) * boomEnv;
            boom = boomLp.process (softClip (boom * 1.3f));
            boomEnv *= boomDecayCoef;
            pitchEnv *= pitchCoef;

            // --- Noise: полосовой шум, слегка декоррелированный по каналам ---
            const float nC = noiseGen.next(), nL = noiseGenL.next(), nR = noiseGenR.next();
            const float nl = noiseHp[0].process (noiseBp[0].process (0.7f * nC + 0.3f * nL)) * noiseEnv;
            const float nr = noiseHp[1].process (noiseBp[1].process (0.7f * nC + 0.3f * nR)) * noiseEnv;
            noiseEnv *= noiseDecayCoef;

            const float addL = sub * boom * 0.9f + noise * nl * 1.6f;
            const float addR = sub * boom * 0.9f + noise * nr * 1.6f;
            L[i] += a * addL;
            R[i] += a * addR;
        }
    }

private:
    // Триггер по фронту огибающей с гистерезисом и «hold-off»
    struct Trigger
    {
        EnvFollower fast, slow;
        int holdOff = 0, holdSamples = 1000;
        void prepare (double sr) { fast.set (0.5f, 40.0f, sr); slow.set (25.0f, 120.0f, sr); holdSamples = (int) (0.045 * sr); }
        void reset() { fast.reset(); slow.reset(); holdOff = 0; }
        bool process (float x, float thr, float& velocity)
        {
            const float f = fast.process (x), s = slow.process (x);
            if (holdOff > 0) { --holdOff; return false; }
            if (f > thr && f > s * 1.6f)
            {
                holdOff = holdSamples;
                velocity = f;
                return true;
            }
            return false;
        }
    };

    double sampleRate = 44100.0, phase = 0.0;
    OnePole lowDet, highDet;
    Trigger boomTrig, noiseTrig;
    float boomEnv = 0.0f, noiseEnv = 0.0f, pitchEnv = 0.0f, lastTone = -1.0f;
    Biquad noiseBp[2], noiseHp[2], boomLp;
    NoiseGen noiseGen { 0x9e3779b9u }, noiseGenL { 0x7f4a7c15u }, noiseGenR { 0x2545f491u };
    Smoother amountSm;

    std::atomic<float>* pSub = nullptr; std::atomic<float>* pTune = nullptr; std::atomic<float>* pDecay = nullptr;
    std::atomic<float>* pNoise = nullptr; std::atomic<float>* pTone = nullptr; std::atomic<float>* pNDecay = nullptr;
    std::atomic<float>* pSens = nullptr;
};
} // namespace pl
