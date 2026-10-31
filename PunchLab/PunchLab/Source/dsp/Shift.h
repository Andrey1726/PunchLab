#pragma once

#include "ModuleSlot.h"

namespace pl
{
// ============================================================================
// Фазовый вокодер с переносом бинов (pitch shift) и коррекцией спектральной
// огибающей (formant shift). Окно Ханна, перекрытие 75 %.
//  ratio   — коэффициент сдвига высоты тона
//  formant — дополнительный коэффициент сдвига формант относительно высоты:
//            1 → форманты движутся вместе с тоном (классический питч-шифт),
//            1/ratio → форманты сохраняются.
// ============================================================================
class PhaseVocoder
{
public:
    void prepare (int fftOrder, double sr)
    {
        order = fftOrder; N = 1 << order; hop = N / 4; latency = N - hop;
        sampleRate = sr;
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) N);
        const int bins = N / 2 + 1;
        inFifo.assign ((size_t) N, 0.0f); outFifo.assign ((size_t) N, 0.0f);
        accum.assign ((size_t) (2 * N), 0.0f); work.assign ((size_t) (2 * N), 0.0f);
        lastPhase.assign ((size_t) bins, 0.0f); sumPhase.assign ((size_t) bins, 0.0f);
        anaMag.assign ((size_t) bins, 0.0f); anaFreq.assign ((size_t) bins, 0.0f);
        synMag.assign ((size_t) bins, 0.0f); synFreq.assign ((size_t) bins, 0.0f);
        env.assign ((size_t) bins, 0.0f); prefix.assign ((size_t) bins + 1, 0.0);
        reset();
    }

    void reset()
    {
        std::fill (inFifo.begin(), inFifo.end(), 0.0f); std::fill (outFifo.begin(), outFifo.end(), 0.0f);
        std::fill (accum.begin(), accum.end(), 0.0f);
        std::fill (lastPhase.begin(), lastPhase.end(), 0.0f); std::fill (sumPhase.begin(), sumPhase.end(), 0.0f);
        rover = latency;
    }

    // Отсчёт, записанный в FIFO, выходит наружу ровно через N сэмплов
    // (3 сдвига по hop внутри FIFO + 1 hop на выдачу из outFifo)
    int getLatency() const { return N; }

    float process (float x, float ratio, float formant)
    {
        inFifo[(size_t) rover] = x;
        const float y = outFifo[(size_t) (rover - latency)];
        if (++rover >= N)
        {
            rover = latency;
            processFrame (ratio, formant);
        }
        return y;
    }

private:
    void processFrame (float ratio, float formant)
    {
        const int bins = N / 2 + 1;
        const float expct = kTwoPi * (float) hop / (float) N;
        const float osamp = (float) (N / hop);

        for (int i = 0; i < N; ++i) work[(size_t) i] = inFifo[(size_t) i] * window[(size_t) i];
        std::fill (work.begin() + N, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);

        // ---- анализ: амплитуда и «истинная» частота каждого бина (в бинах) ----
        for (int k = 0; k < bins; ++k)
        {
            const float re = work[(size_t) (2 * k)], im = work[(size_t) (2 * k + 1)];
            const float mag = std::sqrt (re * re + im * im);
            const float ph  = std::atan2 (im, re);
            float d = ph - lastPhase[(size_t) k];
            lastPhase[(size_t) k] = ph;
            d -= (float) k * expct;
            d = d - kTwoPi * std::round (d / kTwoPi);           // в (-π, π]
            anaMag[(size_t) k]  = mag;
            anaFreq[(size_t) k] = (float) k + d * osamp / kTwoPi; // отклонение в долях бина
        }

        // ---- спектральная огибающая (скользящее среднее амплитуд) ----
        const bool needEnv = std::abs (formant - 1.0f) > 1.0e-3f;
        if (needEnv)
        {
            prefix[0] = 0.0;
            for (int k = 0; k < bins; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + anaMag[(size_t) k];
            const int w = std::max (2, N / 128);
            for (int k = 0; k < bins; ++k)
            {
                const int a = std::max (0, k - w), b = std::min (bins - 1, k + w);
                env[(size_t) k] = (float) ((prefix[(size_t) b + 1] - prefix[(size_t) a]) / (double) (b - a + 1)) + 1.0e-7f;
            }
        }

        // ---- перенос бинов ----
        std::fill (synMag.begin(), synMag.end(), 0.0f);
        std::fill (synFreq.begin(), synFreq.end(), 0.0f);
        for (int k = 0; k < bins; ++k)
        {
            const int idx = (int) std::lround ((float) k * ratio);
            if (idx < 0 || idx >= bins) continue;
            if (anaMag[(size_t) k] >= synMag[(size_t) idx])  // доминирующий бин задаёт частоту
                synFreq[(size_t) idx] = anaFreq[(size_t) k] * ratio;
            synMag[(size_t) idx] += anaMag[(size_t) k];
        }

        // ---- коррекция формант: огибающая после сдвига E(k/ratio) → E(k/(ratio*formant)) ----
        if (needEnv)
        {
            const float invShift  = 1.0f / ratio;
            const float invTarget = 1.0f / (ratio * formant);
            for (int k = 1; k < bins; ++k)
            {
                const float eShift  = envAt ((float) k * invShift, bins);
                const float eTarget = envAt ((float) k * invTarget, bins);
                synMag[(size_t) k] *= juce::jlimit (0.0f, 8.0f, eTarget / eShift);
            }
        }

        // ---- синтез ----
        for (int k = 0; k < bins; ++k)
        {
            const float dev = synFreq[(size_t) k] - (float) k;
            sumPhase[(size_t) k] += (float) k * expct + dev * kTwoPi / osamp;
            sumPhase[(size_t) k] = std::remainder (sumPhase[(size_t) k], kTwoPi);
            const float m = synMag[(size_t) k], p = sumPhase[(size_t) k];
            work[(size_t) (2 * k)]     = m * std::cos (p);
            work[(size_t) (2 * k + 1)] = m * std::sin (p);
        }
        work[1] = 0.0f;                       // DC — вещественный
        work[(size_t) (2 * (N / 2) + 1)] = 0.0f; // Найквист — вещественный
        for (int k = N / 2 + 1; k < N; ++k)   // комплексно-сопряжённая половина
        {
            work[(size_t) (2 * k)]     =  work[(size_t) (2 * (N - k))];
            work[(size_t) (2 * k + 1)] = -work[(size_t) (2 * (N - k) + 1)];
        }
        fft->performRealOnlyInverseTransform (work.data());

        // ---- overlap-add (Hann² при 75 % перекрытии суммируется в 1.5) ----
        const float norm = 1.0f / 1.5f;
        for (int i = 0; i < N; ++i)
            accum[(size_t) i] += work[(size_t) i] * window[(size_t) i] * norm;

        for (int i = 0; i < hop; ++i) outFifo[(size_t) i] = accum[(size_t) i];
        std::memmove (accum.data(), accum.data() + hop, sizeof (float) * (size_t) N);
        std::fill (accum.begin() + N, accum.begin() + N + hop, 0.0f);
        std::memmove (inFifo.data(), inFifo.data() + hop, sizeof (float) * (size_t) latency);
    }

    float envAt (float pos, int bins) const
    {
        if (pos >= (float) (bins - 1)) return env[(size_t) bins - 1];
        const int i = (int) pos; const float f = pos - (float) i;
        return env[(size_t) i] + f * (env[(size_t) i + 1] - env[(size_t) i]);
    }

    int order = 10, N = 1024, hop = 256, latency = 768, rover = 768;
    double sampleRate = 44100.0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, inFifo, outFifo, accum, work, lastPhase, sumPhase, anaMag, anaFreq, synMag, synFreq, env;
    std::vector<double> prefix;
};

// ============================================================================
// Shift: Pitch (±24 полутона), Formant (±12 полутонов, относительно сдвига
// тона: 0 = форманты следуют за тоном), Freq — частотный сдвиг (SSB,
// неравномерный сдвиг гармоник → металлические, «колокольные» тембры).
// ============================================================================
class Shift : public EffectModule
{
public:
    void bind (juce::AudioProcessorValueTreeState& s) override
    {
        pPitch = s.getRawParameterValue (ID::shiftPitch);
        pFormant = s.getRawParameterValue (ID::shiftFormant);
        pFreq = s.getRawParameterValue (ID::shiftFreq);
    }

    void prepare (double sr, int maxBlock) override
    {
        sampleRate = sr;
        const int order = sr > 64000.0 ? 11 : 10;  // ~21 мс при 48 кГц
        for (auto& v : pv) v.prepare (order, sr);
        dryDelay.prepare (pv[0].getLatency(), maxBlock);
        amountSm.prepare (sr, 30.0f);
        ratioSm.prepare (sr, 40.0f);
        formSm.prepare (sr, 40.0f);
        freqSm.prepare (sr, 30.0f);
        reset();
    }

    void reset() override
    {
        for (auto& v : pv) v.reset();
        for (auto& h : hilbert) h.reset();
        dryDelay.reset();
        amountSm.reset (0.0f);
        ratioSm.reset (std::pow (2.0f, pPitch->load() / 12.0f));
        formSm.reset (std::pow (2.0f, pFormant->load() / 12.0f));
        freqSm.reset (pFreq->load());
        oscPhase = 0.0;
    }

    int getLatency() const override { return pv[0].getLatency(); }

    void process (float* L, float* R, int n, float amount, const ProcessContext&) override
    {
        amountSm.setTarget (amount);
        ratioSm.setTarget (std::pow (2.0f, pPitch->load() / 12.0f));
        formSm.setTarget (std::pow (2.0f, pFormant->load() / 12.0f));
        freqSm.setTarget (pFreq->load());

        for (int i = 0; i < n; ++i)
        {
            const float a = amountSm.next(), ratio = ratioSm.next(), form = formSm.next(), fs = freqSm.next();
            const float inL = L[i], inR = R[i];

            float wl = pv[0].process (inL, ratio, form);
            float wr = pv[1].process (inR, ratio, form);

            // частотный сдвиг: Re{ (x + jH{x}) · e^{jωt} }
            float re, im;
            hilbert[0].process (wl, re, im);
            float c = std::cos ((float) oscPhase * kTwoPi), s = std::sin ((float) oscPhase * kTwoPi);
            const float fl = re * c - im * s;
            hilbert[1].process (wr, re, im);
            const float fr = re * c - im * s;
            oscPhase += (double) fs / sampleRate;
            oscPhase -= std::floor (oscPhase);
            const float fsMix = juce::jlimit (0.0f, 1.0f, std::abs (fs) / 5.0f); // плавный вход в сдвиг около 0 Гц
            wl += fsMix * (fl - wl);
            wr += fsMix * (fr - wr);

            float dl = inL, dr = inR;
            dryDelay.process (&dl, &dr, 1);
            L[i] = dl + a * (wl - dl);
            R[i] = dr + a * (wr - dr);
        }
    }

private:
    double sampleRate = 44100.0, oscPhase = 0.0;
    PhaseVocoder pv[2];
    HilbertTransformer hilbert[2];
    StereoDelay dryDelay;
    Smoother amountSm, ratioSm, formSm, freqSm;
    std::atomic<float>* pPitch = nullptr; std::atomic<float>* pFormant = nullptr; std::atomic<float>* pFreq = nullptr;
};
} // namespace pl
