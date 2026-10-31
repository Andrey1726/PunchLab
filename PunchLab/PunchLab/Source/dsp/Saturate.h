#pragma once

#include "ModuleSlot.h"

namespace pl
{
// ============================================================================
// Saturate — нелинейная обработка с 4-кратной передискретизацией (FIR-
// полуполосные фильтры, целочисленная латентность — компенсируется).
// Режимы: Tape, Tube (асимметрия → чётные гармоники), Transistor (кубический
// клиппер), Fuzz (выпрямление + жёсткий драйв), Fold (волновой фолдер),
// Crush (понижение разрядности и частоты дискретизации).
// Amount — доля обработанного сигнала, Drive — степень искажения, Tone — наклон АЧХ.
// ============================================================================
class Saturate : public EffectModule
{
public:
    void bind (juce::AudioProcessorValueTreeState& s) override
    {
        pMode = s.getRawParameterValue (ID::satMode);
        pDrive = s.getRawParameterValue (ID::satDrive);
        pTone = s.getRawParameterValue (ID::satTone);
    }

    void prepare (double sr, int maxBlock) override
    {
        sampleRate = sr;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os->initProcessing ((size_t) maxBlock);
        latency = (int) std::lround (os->getLatencyInSamples());
        dryDelay.prepare (latency, maxBlock);
        dryBuf.setSize (2, maxBlock, false, false, true);
        for (auto& d : dc) d.prepare (sr);
        for (auto& t : tilt) t.setCutoff (900.0f, sr);
        for (auto& t : tapeLp) t.setCutoff (14000.0f, sr * 4.0);
        amountSm.prepare (sr, 30.0f); driveSm.prepare (sr * 4.0, 30.0f); toneSm.prepare (sr, 30.0f);
        reset();
    }

    void reset() override
    {
        if (os) os->reset();
        dryDelay.reset();
        for (auto& d : dc) d.reset();
        for (auto& t : tilt) t.reset();
        for (auto& t : tapeLp) t.reset();
        holdCount = 0; held[0] = held[1] = 0.0f;
        amountSm.reset (0.0f); driveSm.reset (pDrive->load()); toneSm.reset (pTone->load());
    }

    int getLatency() const override { return latency; }

    void process (float* L, float* R, int n, float amount, const ProcessContext&) override
    {
        amountSm.setTarget (amount);
        driveSm.setTarget (pDrive->load());
        toneSm.setTarget (pTone->load());
        const int mode = (int) pMode->load();

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
            const float drive = driveSm.next();
            if (mode == 5)  // Crush
            {
                const float bits = 16.0f - 13.0f * drive;
                const float step = std::pow (2.0f, 1.0f - bits);
                const int hold = 1 + (int) (drive * drive * 40.0f) * 4;
                if (holdCount <= 0) { held[0] = uL[i]; held[1] = uR[i]; holdCount = hold; }
                --holdCount;
                uL[i] = std::round (held[0] / step) * step;
                uR[i] = std::round (held[1] / step) * step;
                continue;
            }
            const float g = dbToGain (drive * 36.0f);
            uL[i] = shape (mode, uL[i], g, drive, 0);
            uR[i] = shape (mode, uR[i], g, drive, 1);
        }

        os->processSamplesDown (block);

        float* dL = dryBuf.getWritePointer (0); float* dR = dryBuf.getWritePointer (1);
        dryDelay.process (dL, dR, n);

        for (int i = 0; i < n; ++i)
        {
            const float a = amountSm.next(), tone = toneSm.next();
            const float lowG = dbToGain (-tone * 6.0f), highG = dbToGain (tone * 6.0f);
            float wl = dc[0].process (L[i]), wr = dc[1].process (R[i]);
            const float ll = tilt[0].lp (wl), lr = tilt[1].lp (wr);
            wl = ll * lowG + (wl - ll) * highG;
            wr = lr * lowG + (wr - lr) * highG;
            L[i] = dL[i] + a * (wl - dL[i]);
            R[i] = dR[i] + a * (wr - dR[i]);
        }
    }

private:
    float shape (int mode, float x, float g, float drive, int ch)
    {
        const float comp = std::pow (g, -0.55f);
        switch (mode)
        {
            case 0: // Tape: мягкое насыщение + завал ВЧ, растущий с драйвом
            {
                float y = std::tanh (x * g * 0.8f) * 1.25f;
                const float lp = tapeLp[ch].lp (y);
                y += drive * 0.6f * (lp - y);
                return y * comp;
            }
            case 1: // Tube: асимметричная характеристика (смещение рабочей точки)
            {
                const float b = 0.25f + 0.25f * drive;
                return (std::tanh (x * g + b) - std::tanh (b)) * comp * 1.2f;
            }
            case 2: // Transistor: кубический мягко-жёсткий клиппер
            {
                const float v = juce::jlimit (-1.0f, 1.0f, x * g * 0.7f);
                return (1.5f * v - 0.5f * v * v * v) * comp * 1.1f;
            }
            case 3: // Fuzz: частичное выпрямление + жёсткое ограничение
            {
                const float v = x * g * 2.0f;
                const float r = v + 0.7f * std::abs (v);
                return juce::jlimit (-1.0f, 1.0f, std::tanh (r * 1.5f)) * comp * 0.9f;
            }
            case 4: // Fold: синусоидальный фолдер
            {
                const float v = x * (1.0f + (g - 1.0f) * 0.35f);
                return std::sin (v * 1.5707963f) * std::min (1.0f, 1.0f / (0.6f + 0.4f * std::sqrt (g)));
            }
            default: return x;
        }
    }

    double sampleRate = 44100.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    int latency = 0;
    StereoDelay dryDelay;
    juce::AudioBuffer<float> dryBuf;
    DcBlocker dc[2];
    OnePole tilt[2], tapeLp[2];
    int holdCount = 0; float held[2] {};
    Smoother amountSm, driveSm, toneSm;
    std::atomic<float>* pMode = nullptr; std::atomic<float>* pDrive = nullptr; std::atomic<float>* pTone = nullptr;
};
} // namespace pl
