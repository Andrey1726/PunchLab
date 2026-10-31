#pragma once

#include "ModuleSlot.h"

namespace pl
{
// ============================================================================
// More — повышение воспринимаемой громкости и плотности: входной гейн до
// +18 дБ, вейвшейпер с морфингом мягкое (tanh) → «кирпичное» ограничение,
// финальный клиппер на уровне Ceiling. 4x передискретизация против алиасинга.
// ============================================================================
class More : public EffectModule
{
public:
    void bind (juce::AudioProcessorValueTreeState& s) override
    {
        pChar = s.getRawParameterValue (ID::moreChar);
        pCeil = s.getRawParameterValue (ID::moreCeil);
    }

    void prepare (double sr, int maxBlock) override
    {
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os->initProcessing ((size_t) maxBlock);
        latency = (int) std::lround (os->getLatencyInSamples());
        dryDelay.prepare (latency, maxBlock);
        dryBuf.setSize (2, maxBlock, false, false, true);
        amountSm.prepare (sr * 4.0, 30.0f); charSm.prepare (sr * 4.0, 30.0f); ceilSm.prepare (sr * 4.0, 30.0f);
        mixSm.prepare (sr, 30.0f);
        reset();
    }

    void reset() override
    {
        if (os) os->reset();
        dryDelay.reset();
        amountSm.reset (0.0f); mixSm.reset (0.0f);
        charSm.reset (pChar->load()); ceilSm.reset (dbToGain (pCeil->load()));
    }

    int getLatency() const override { return latency; }

    void process (float* L, float* R, int n, float amount, const ProcessContext&) override
    {
        amountSm.setTarget (amount);
        mixSm.setTarget (std::min (1.0f, amount * 5.0f));
        charSm.setTarget (pChar->load());
        ceilSm.setTarget (dbToGain (pCeil->load()));

        juce::FloatVectorOperations::copy (dryBuf.getWritePointer (0), L, n);
        juce::FloatVectorOperations::copy (dryBuf.getWritePointer (1), R, n);

        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
        auto up = os->processSamplesUp (block);
        const int un = (int) up.getNumSamples();
        float* uL = up.getChannelPointer (0);
        float* uR = up.getChannelPointer (1);

        for (int i = 0; i < un; ++i)
        {
            const float a = amountSm.next(), ch = charSm.next(), ceil = ceilSm.next();
            const float g = dbToGain (a * 18.0f);
            uL[i] = shape (uL[i] * g / ceil, ch) * ceil;
            uR[i] = shape (uR[i] * g / ceil, ch) * ceil;
        }

        os->processSamplesDown (block);

        float* dL = dryBuf.getWritePointer (0); float* dR = dryBuf.getWritePointer (1);
        dryDelay.process (dL, dR, n);
        for (int i = 0; i < n; ++i)
        {
            const float m = mixSm.next();
            L[i] = dL[i] + m * (L[i] - dL[i]);
            R[i] = dR[i] + m * (R[i] - dR[i]);
        }
    }

private:
    // character: 0 = мягкий вейвшейпинг, 1 = жёсткий клиппинг
    static float shape (float x, float character)
    {
        // мягкая ветвь: «sine-ish» насыщение с плавным коленом
        const float soft = softClip (x * 1.1f) / softClip (1.1f) * 0.98f;
        // жёсткая ветвь: клиппер с узким коленом
        const float knee = 0.1f;
        const float ax = std::abs (x);
        float hard;
        if (ax < 1.0f - knee) hard = x;
        else if (ax < 1.0f + knee) { const float t = ax - (1.0f - knee); hard = std::copysign (ax - t * t / (4.0f * knee), x); }
        else hard = std::copysign (1.0f, x);
        const float y = soft + character * (hard - soft);
        return juce::jlimit (-1.0f, 1.0f, y);
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    int latency = 0;
    StereoDelay dryDelay;
    juce::AudioBuffer<float> dryBuf;
    Smoother amountSm, charSm, ceilSm, mixSm;
    std::atomic<float>* pChar = nullptr; std::atomic<float>* pCeil = nullptr;
};
} // namespace pl
