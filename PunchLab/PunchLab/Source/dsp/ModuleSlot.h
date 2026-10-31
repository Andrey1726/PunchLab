#pragma once

#include "DspUtils.h"
#include "../Params.h"

namespace pl
{
// ============================================================================
// Базовый интерфейс эффектного модуля. Обработка стерео in-place.
// amount — итоговая интенсивность (Amount модуля * Magnitude), 0..1.
// ============================================================================
class EffectModule
{
public:
    virtual ~EffectModule() = default;
    virtual void bind (juce::AudioProcessorValueTreeState& apvts) = 0;
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    virtual void reset() = 0;
    virtual void process (float* L, float* R, int n, float amount, const ProcessContext& ctx) = 0;
    virtual int  getLatency() const { return 0; }
};

// ============================================================================
// Target: разделение сигнала на транзиентную и сустейновую части.
// Используется разность «быстрой» и «медленной» огибающих (как в transient
// designer). g ∈ [0,1] — доля сигнала, относимая к транзиенту, t = g*x, s = (1-g)*x,
// поэтому t + s = x точно (без фазовых искажений).
// ============================================================================
class TransientSplitter
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        fast.set (0.3f, 60.0f, sr);
        slow.set (12.0f, 60.0f, sr);
        gAtk = msToCoef (0.3f, sr);
        setLength (40.0f, 0.5f);
        reset();
    }
    void reset() { fast.reset(); slow.reset(); g = 0.0f; }

    void setLength (float ms, float sensitivity)
    {
        if (ms != lastMs)  { gRel = msToCoef (ms, sampleRate); lastMs = ms; }
        sens = 1.0f + 7.0f * sensitivity;
    }

    float process (float detector)  // detector = max(|L|,|R|)
    {
        const float f = fast.process (detector);
        const float s = slow.process (detector);
        float target = (f - s) / (f + 1.0e-6f) * sens;
        target = juce::jlimit (0.0f, 1.0f, target);
        const float c = target > g ? gAtk : gRel;
        g = target + c * (g - target);
        return g;
    }
private:
    EnvFollower fast, slow;
    double sampleRate = 44100.0;
    float g = 0.0f, gAtk = 0.0f, gRel = 0.0f, sens = 4.5f, lastMs = -1.0f;
};

// ============================================================================
// Focus: трёхполосный кроссовер Линквица-Райли (LR4). Обрабатывается только
// средняя полоса [lo, hi]; низ/верх идут мимо. Низкая полоса проходит через
// фазовый корректор (allpass) на частоте hi — сумма полос даёт плоскую АЧХ.
// ============================================================================
class FocusSplitter
{
public:
    void prepare (double sr, int maxBlock)
    {
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
        for (auto* f : { &xLo, &xHi, &apLo }) { f->prepare (spec); }
        apLo.setType (juce::dsp::LinkwitzRileyFilterType::allpass);
        sampleRate = sr;
        setRange (20.0f, 20000.0f, true);
    }
    void reset() { xLo.reset(); xHi.reset(); apLo.reset(); }

    void setRange (float lo, float hi, bool force = false)
    {
        const float nyq = (float) sampleRate * 0.45f;
        lo = juce::jlimit (10.0f, nyq * 0.8f, lo);
        hi = juce::jlimit (lo * 1.15f, nyq, hi);
        if (force || std::abs (lo - curLo) > 0.01f) { xLo.setCutoffFrequency (lo); curLo = lo; }
        if (force || std::abs (hi - curHi) > 0.01f) { xHi.setCutoffFrequency (hi); apLo.setCutoffFrequency (hi); curHi = hi; }
    }

    // ch: 0/1
    inline void split (int ch, float x, float& low, float& mid, float& high)
    {
        float rest;
        xLo.processSample (ch, x, low, rest);
        xHi.processSample (ch, rest, mid, high);
        low = apLo.processSample (ch, low);
    }
private:
    juce::dsp::LinkwitzRileyFilter<float> xLo, xHi, apLo;
    double sampleRate = 44100.0;
    float curLo = -1.0f, curHi = -1.0f;
};

// ============================================================================
// Слот цепочки: модуль + Focus + Target + плавное вкл/выкл + компенсация
// латентности для обходных компонентов сигнала.
// ============================================================================
class ModuleSlot
{
public:
    ModuleSlot (int moduleIndex, std::unique_ptr<EffectModule> m)
        : index (moduleIndex), fx (std::move (m)) {}

    void bind (juce::AudioProcessorValueTreeState& apvts)
    {
        fx->bind (apvts);
        pOn      = apvts.getRawParameterValue (moduleParam (index, "on"));
        pAmount  = apvts.getRawParameterValue (moduleParam (index, "amount"));
        pTarget  = apvts.getRawParameterValue (moduleParam (index, "target"));
        pFocus   = apvts.getRawParameterValue (moduleParam (index, "focus"));
        pFocusLo = apvts.getRawParameterValue (moduleParam (index, "focus_lo"));
        pFocusHi = apvts.getRawParameterValue (moduleParam (index, "focus_hi"));
    }

    void prepare (double sr, int maxBlock)
    {
        fx->prepare (sr, maxBlock);
        focus.prepare (sr, maxBlock);
        splitter.prepare (sr);
        latency = fx->getLatency();
        passDelay.prepare (latency, maxBlock);
        dryDelay.prepare (latency, maxBlock);
        for (auto* b : { &dry, &path, &pass }) b->setSize (2, maxBlock, false, false, true);
        enable.prepare (sr, 15.0f);
        enable.reset (pOn->load() > 0.5f ? 1.0f : 0.0f);
    }

    void reset()
    {
        fx->reset(); focus.reset(); splitter.reset(); passDelay.reset(); dryDelay.reset();
    }

    int  getLatency() const     { return latency; }
    int  getModuleIndex() const { return index; }
    EffectModule* getModule()   { return fx.get(); }

    void process (float* L, float* R, int n, float magnitude, float splitMs, float splitSens, const ProcessContext& ctx)
    {
        const bool on = pOn->load() > 0.5f;
        enable.setTarget (on ? 1.0f : 0.0f);

        // Полностью выключен: только компенсируем латентность модуля
        if (! on && ! enable.isSmoothing() && enable.current <= 0.0f)
        {
            dryDelay.process (L, R, n);
            return;
        }

        const float amount = juce::jlimit (0.0f, 1.0f, pAmount->load() * magnitude);
        const int   target = (int) pTarget->load();
        const bool  useFocus = pFocus->load() > 0.5f;

        float* dL = dry.getWritePointer (0);  float* dR = dry.getWritePointer (1);
        float* pL = path.getWritePointer (0); float* pR = path.getWritePointer (1);
        float* qL = pass.getWritePointer (0); float* qR = pass.getWritePointer (1);

        juce::FloatVectorOperations::copy (dL, L, n);
        juce::FloatVectorOperations::copy (dR, R, n);

        bool hasPass = false;

        // ---- Focus ----
        if (useFocus)
        {
            focus.setRange (pFocusLo->load(), pFocusHi->load());
            for (int i = 0; i < n; ++i)
            {
                float lo, mi, hi;
                focus.split (0, L[i], lo, mi, hi); pL[i] = mi; qL[i] = lo + hi;
                focus.split (1, R[i], lo, mi, hi); pR[i] = mi; qR[i] = lo + hi;
            }
            hasPass = true;
        }
        else
        {
            juce::FloatVectorOperations::copy (pL, L, n);
            juce::FloatVectorOperations::copy (pR, R, n);
        }

        // ---- Target ----
        if (target != kTargetFull)
        {
            splitter.setLength (splitMs, splitSens);
            if (! hasPass) { juce::FloatVectorOperations::clear (qL, n); juce::FloatVectorOperations::clear (qR, n); }
            for (int i = 0; i < n; ++i)
            {
                const float g  = splitter.process (std::max (std::abs (pL[i]), std::abs (pR[i])));
                const float tl = pL[i] * g, tr = pR[i] * g;
                const float sl = pL[i] - tl, sr = pR[i] - tr;
                if (target == kTargetTransient) { pL[i] = tl; pR[i] = tr; qL[i] += sl; qR[i] += sr; }
                else                            { pL[i] = sl; pR[i] = sr; qL[i] += tl; qR[i] += tr; }
            }
            hasPass = true;
        }
        else if (useFocus)
        {
            // держим детектор «тёплым», чтобы не было щелчка при переключении
            for (int i = 0; i < n; ++i) splitter.process (std::max (std::abs (pL[i]), std::abs (pR[i])));
        }

        // ---- Модуль ----
        fx->process (pL, pR, n, amount, ctx);

        // ---- Сборка ----
        if (hasPass)
        {
            passDelay.process (qL, qR, n);
            juce::FloatVectorOperations::add (pL, qL, n);
            juce::FloatVectorOperations::add (pR, qR, n);
        }
        dryDelay.process (dL, dR, n);

        if (enable.isSmoothing() || enable.current < 1.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float e = enable.next();
                L[i] = dL[i] + e * (pL[i] - dL[i]);
                R[i] = dR[i] + e * (pR[i] - dR[i]);
            }
        }
        else
        {
            juce::FloatVectorOperations::copy (L, pL, n);
            juce::FloatVectorOperations::copy (R, pR, n);
        }
    }

private:
    int index;
    std::unique_ptr<EffectModule> fx;
    FocusSplitter focus;
    TransientSplitter splitter;
    StereoDelay passDelay, dryDelay;
    juce::AudioBuffer<float> dry, path, pass;
    Smoother enable;
    int latency = 0;

    std::atomic<float>* pOn = nullptr; std::atomic<float>* pAmount = nullptr;
    std::atomic<float>* pTarget = nullptr; std::atomic<float>* pFocus = nullptr;
    std::atomic<float>* pFocusLo = nullptr; std::atomic<float>* pFocusHi = nullptr;
};
} // namespace pl
