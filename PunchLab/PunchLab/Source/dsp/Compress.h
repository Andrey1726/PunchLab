#pragma once

#include "ModuleSlot.h"

namespace pl
{
// ============================================================================
// Compress — feed-forward компрессор со стерео-связанным детектором.
// Режимы:
//   Glue  — мягкая «склейка» шины (RMS, ratio 2.5, медленная атака, авто-релиз)
//   Punch — медленная атака пропускает транзиент → усиление атаки
//   Smash — «all buttons in»: мгновенная атака, ratio 20, лёгкий драйв
//   Pump  — длинный релиз, детектор по НЧ → пампинг от бочки
//   Opto  — программно-зависимый двухступенчатый релиз, мягкое колено
//   Limit — быстрый лимитер-подобный режим
// Amount задаёт глубину (порог), Speed масштабирует атаку/релиз, Mix — параллельная смесь.
// ============================================================================
class Compress : public EffectModule
{
public:
    std::atomic<float> gainReductionDb { 0.0f }; // для индикатора в GUI

    void bind (juce::AudioProcessorValueTreeState& s) override
    {
        pMode = s.getRawParameterValue (ID::compMode);
        pSpeed = s.getRawParameterValue (ID::compSpeed);
        pMix = s.getRawParameterValue (ID::compMix);
    }

    void prepare (double sr, int) override
    {
        sampleRate = sr;
        amountSm.prepare (sr, 40.0f); mixSm.prepare (sr, 40.0f);
        reset();
    }

    void reset() override
    {
        envDb = 0.0f; rms = 0.0f; slowRel = 0.0f; grSlow = 0.0f;
        scHp.reset(); scHp2.reset();
        amountSm.reset (0.0f); mixSm.reset (pMix->load());
        lastMode = -1;
    }

    void process (float* L, float* R, int n, float amount, const ProcessContext&) override
    {
        amountSm.setTarget (amount);
        mixSm.setTarget (pMix->load());

        const int mode = (int) pMode->load();
        const float speed = pSpeed->load();
        if (mode != lastMode || speed != lastSpeed) configure (mode, speed);

        float maxGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float a = amountSm.next(), mix = mixSm.next();
            const float thr = -36.0f * a;                         // порог

            // --- сайдчейн ---
            float sl = L[i], sr = R[i];
            if (scHpOn) { sl = scHp.process (sl); sr = scHp2.process (sr); }
            float det = std::max (std::abs (sl), std::abs (sr));
            if (useRms)
            {
                rms = det * det + rmsCoef * (rms - det * det);
                det = std::sqrt (rms);
            }
            const float lvl = gainToDb (det);

            // --- статическая характеристика с мягким коленом ---
            const float over = lvl - thr;
            float gc;
            if (2.0f * over < -knee)              gc = 0.0f;
            else if (2.0f * std::abs (over) <= knee)
            {
                const float t = over + 0.5f * knee;
                gc = (1.0f / ratio - 1.0f) * t * t / (2.0f * knee);
            }
            else                                  gc = (1.0f / ratio - 1.0f) * over;

            // --- баллистика ---
            float rel = relCoef;
            if (mode == 4)  // Opto: чем дольше и глубже компрессия, тем медленнее релиз
            {
                slowRel = std::min (slowRel + 1.0f / (float) sampleRate, 1.0f) * (gc < -1.0f ? 1.0f : 0.9995f);
                rel = relCoef + (relSlowCoef - relCoef) * slowRel;
            }
            else if (mode == 0) // Glue: авто-релиз — быстрее на коротких пиках
            {
                rel = gc < envDb - 3.0f ? relCoef : relSlowCoef;
            }
            const float c = gc < envDb ? atkCoef : rel;
            envDb = gc + c * (envDb - gc);
            maxGr = std::min (maxGr, envDb);

            // адаптивная компенсация: по усреднённому (≈0.5 с) подавлению,
            // поэтому пропущенные режимом Punch транзиенты не «улетают» по уровню
            grSlow = envDb + slowCoef * (grSlow - envDb);
            // + статическая часть по порогу (для пиковых режимов вроде Limit,
            //   где среднее подавление заметно меньше пикового)
            const float makeup = juce::jlimit (0.0f, 24.0f, -grSlow * makeupAmt - thr * (1.0f - 1.0f / ratio) * staticAmt);

            const float g = dbToGain (envDb + makeup);
            float wl = L[i] * g, wr = R[i] * g;
            if (drive > 0.0f) { wl = softClip (wl * (1.0f + drive)) / (1.0f + 0.5f * drive); wr = softClip (wr * (1.0f + drive)) / (1.0f + 0.5f * drive); }

            // Amount=0 → гарантированно без изменений
            const float wet = mix * std::min (1.0f, a * 8.0f);
            L[i] += wet * (wl - L[i]);
            R[i] += wet * (wr - R[i]);
        }
        gainReductionDb.store (maxGr);
    }

private:
    void configure (int mode, float speed)
    {
        lastMode = mode; lastSpeed = speed;
        float atkMs = 10.0f, relMs = 100.0f, relSlowMs = 400.0f, scHz = 0.0f;
        useRms = false; drive = 0.0f; makeupAmt = 0.5f; staticAmt = 0.2f;
        switch (mode)
        {
            case 0: atkMs = 10.0f;  relMs = 80.0f;  relSlowMs = 500.0f; ratio = 2.5f; knee = 8.0f;  useRms = true; scHz = 60.0f; makeupAmt = 0.6f; staticAmt = 0.05f; break; // Glue
            case 1: atkMs = 28.0f;  relMs = 60.0f;  relSlowMs = 60.0f;  ratio = 5.0f; knee = 4.0f;  makeupAmt = 0.35f; staticAmt = 0.05f; break;                         // Punch
            case 2: atkMs = 0.08f;  relMs = 70.0f;  relSlowMs = 70.0f;  ratio = 20.0f; knee = 2.0f; drive = 0.6f; makeupAmt = 0.6f; staticAmt = 0.3f; break;          // Smash
            case 3: atkMs = 3.0f;   relMs = 320.0f; relSlowMs = 320.0f; ratio = 10.0f; knee = 3.0f; scHz = 0.0f; makeupAmt = 0.5f; staticAmt = 0.03f; break;           // Pump
            case 4: atkMs = 12.0f;  relMs = 70.0f;  relSlowMs = 1200.0f;ratio = 3.0f; knee = 12.0f; useRms = true; makeupAmt = 0.5f; staticAmt = 0.03f; break;         // Opto
            default:atkMs = 0.02f;  relMs = 35.0f;  relSlowMs = 35.0f;  ratio = 50.0f; knee = 1.0f; makeupAmt = 0.6f; staticAmt = 0.35f; break;                        // Limit
        }
        atkCoef = msToCoef (atkMs * speed, sampleRate);
        relCoef = msToCoef (relMs * speed, sampleRate);
        relSlowCoef = msToCoef (relSlowMs * speed, sampleRate);
        rmsCoef = msToCoef (8.0f, sampleRate);
        slowCoef = msToCoef (500.0f, sampleRate);

        // Pump: детектор слушает только низ (бочку)
        if (mode == 3) { scHp.setLowPass (sampleRate, 150.0, 0.707); scHp2.setLowPass (sampleRate, 150.0, 0.707); scHpOn = true; }
        else if (scHz > 0.0f) { scHp.setHighPass (sampleRate, scHz, 0.707); scHp2.setHighPass (sampleRate, scHz, 0.707); scHpOn = true; }
        else scHpOn = false;
    }

    double sampleRate = 44100.0;
    int lastMode = -1; float lastSpeed = -1.0f;
    float atkCoef = 0, relCoef = 0, relSlowCoef = 0, rmsCoef = 0, ratio = 4, knee = 6, drive = 0, makeupAmt = 0.5f, staticAmt = 0.2f;
    bool useRms = false, scHpOn = false;
    float envDb = 0.0f, rms = 0.0f, slowRel = 0.0f, grSlow = 0.0f, slowCoef = 0.0f;
    Biquad scHp, scHp2;
    Smoother amountSm, mixSm;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pSpeed = nullptr; std::atomic<float>* pMix = nullptr;
};
} // namespace pl
