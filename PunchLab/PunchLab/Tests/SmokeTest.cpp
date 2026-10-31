// Офлайн smoke-тест: рендер синтетического барабанного лупа через процессор.
// Проверяет: отсутствие NaN/Inf, прозрачность при выключенных модулях
// (с учётом латентности), прозрачность фазового вокодера при нулевом сдвиге,
// уровни на всех заводских пресетах и каждом модуле по отдельности.
#include "PluginProcessor.h"
#include <cstdio>

static juce::AudioBuffer<float> makeLoop (double sr, double seconds)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random rnd (42);
    const double beat = 0.5; // 120 BPM
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const double tb = std::fmod (t, beat);
        const int beatIdx = (int) (t / beat);
        float s = 0.0f;
        // бочка на каждую долю
        s += (float) (0.8 * std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 * tb + 60.0 * (1.0 - std::exp (-tb * 30.0)) / 30.0 * 30.0 / 30.0)) * std::exp (-tb * 9.0));
        // малый на 2 и 4
        if (beatIdx % 2 == 1) s += (float) ((rnd.nextFloat() * 2.0f - 1.0f) * 0.5 * std::exp (-tb * 25.0) + 0.3 * std::sin (2 * juce::MathConstants<double>::pi * 190 * tb) * std::exp (-tb * 20.0));
        // хэты восьмыми
        const double th = std::fmod (t, beat / 2.0);
        s += (float) ((rnd.nextFloat() * 2.0f - 1.0f) * 0.15 * std::exp (-th * 80.0));
        b.setSample (0, i, s);
        b.setSample (1, i, s * 0.95f);
    }
    return b;
}

struct Stats { float peak = 0, rms = 0; bool finite = true; };

static Stats render (PunchLabAudioProcessor& p, const juce::AudioBuffer<float>& in, juce::AudioBuffer<float>* outCopy = nullptr, int block = 480)
{
    juce::AudioBuffer<float> io (in);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < io.getNumSamples(); pos += block)
    {
        const int len = std::min (block, io.getNumSamples() - pos);
        juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, pos, len);
        p.processBlock (view, midi);
    }
    Stats s; double acc = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < io.getNumSamples(); ++i)
        {
            const float v = io.getSample (ch, i);
            if (! std::isfinite (v)) s.finite = false;
            s.peak = std::max (s.peak, std::abs (v));
            acc += v * v;
        }
    s.rms = (float) std::sqrt (acc / (2.0 * io.getNumSamples()));
    if (outCopy) *outCopy = io;
    return s;
}

static void setParam (PunchLabAudioProcessor& p, const juce::String& id, float value)
{
    auto* rp = p.apvts.getParameter (id);
    rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

static float maxDiffAligned (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int latency, int skip)
{
    float m = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = skip; i < in.getNumSamples() - latency; ++i)
            m = std::max (m, std::abs (out.getSample (ch, i + latency) - in.getSample (ch, i)));
    return m;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
    {
        PunchLabAudioProcessor p;
        p.prepareToPlay (48000.0, 512);
        p.setCurrentProgram (argc > 3 ? juce::String (argv[3]).getIntValue() : 5);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (1080, 720);
        auto loop = makeLoop (48000.0, 1.0);
        render (p, loop);
        for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("snapshot written: %s\n", f.getFullPathName().toRawUTF8());
        return 0;
    }
    const double sr = 48000.0;
    auto loop = makeLoop (sr, 4.0);
    Stats ref; { double acc = 0; for (int i = 0; i < loop.getNumSamples(); ++i) { ref.peak = std::max (ref.peak, std::abs (loop.getSample (0, i))); acc += loop.getSample (0, i) * loop.getSample (0, i); } ref.rms = (float) std::sqrt (acc / loop.getNumSamples()); }
    std::printf ("input: peak %.3f rms %.3f\n", ref.peak, ref.rms);

    int failures = 0;

    // ---- 0. фазовый вокодер напрямую ----
    {
        pl::PhaseVocoder pv; pv.prepare (10, sr);
        std::vector<float> in (20000), out (20000);
        for (int i = 0; i < 20000; ++i) in[(size_t) i] = 0.5f * std::sin (0.05f * i) + 0.2f * std::sin (0.31f * i);
        for (int i = 0; i < 20000; ++i) out[(size_t) i] = pv.process (in[(size_t) i], 1.0f, 1.0f);
        int bestLag = 0; float bestErr = 1e9f;
        for (int lag = 0; lag < 2048; ++lag)
        {
            float e = 0; for (int i = 4000; i < 16000; ++i) e = std::max (e, std::abs (out[(size_t) (i + lag)] - in[(size_t) i]));
            if (e < bestErr) { bestErr = e; bestLag = lag; }
        }
        double ri = 0, ro = 0; for (int i = 4000; i < 16000; ++i) { ri += in[(size_t) i] * in[(size_t) i]; ro += out[(size_t) i] * out[(size_t) i]; }
        const bool ok = bestLag == pv.getLatency() && bestErr < 1e-3f;
        std::printf ("[pv direct] reported latency %d, best lag %d, err %.4f, gain %.3f %s\n", pv.getLatency(), bestLag, bestErr, std::sqrt (ro / ri), ok ? "OK" : "FAIL");
        if (! ok) ++failures;
    }

    // ---- 1. прозрачность (все модули выкл.) ----
    {
        PunchLabAudioProcessor p;
        p.prepareToPlay (sr, 512);
        const int lat = p.getLatencySamples();
        juce::AudioBuffer<float> out;
        render (p, loop, &out);
        const float d = maxDiffAligned (loop, out, lat, 0);
        std::printf ("[transparency] latency %d samples, max diff %.2e %s\n", lat, d, d < 1e-5f ? "OK" : "FAIL");
        if (d >= 1e-5f) ++failures;
    }

    // ---- 2. фазовый вокодер при нулевом сдвиге ----
    {
        PunchLabAudioProcessor p;
        p.prepareToPlay (sr, 512);
        setParam (p, "shift_on", 1); setParam (p, "shift_amount", 1.0f);
        const int lat = p.getLatencySamples();
        juce::AudioBuffer<float> out;
        render (p, loop, &out);
        const float d = maxDiffAligned (loop, out, lat, 4096);
        std::printf ("[pv identity] max diff %.4f %s\n", d, d < 0.02f ? "OK" : "FAIL");
        if (d >= 0.02f) ++failures;
    }

    // ---- 3. Target: сумма транзиента и сустейна ----
    {
        PunchLabAudioProcessor p;
        p.prepareToPlay (sr, 512);
        setParam (p, "comp_on", 1); setParam (p, "comp_amount", 0.0f); setParam (p, "comp_target", 1);
        setParam (p, "comp_focus", 0);
        juce::AudioBuffer<float> out;
        render (p, loop, &out);
        const float d = maxDiffAligned (loop, out, p.getLatencySamples(), 0);
        std::printf ("[target split null] max diff %.2e %s\n", d, d < 1e-4f ? "OK" : "FAIL");
        if (d >= 1e-4f) ++failures;
    }

    // ---- 4. каждый модуль отдельно ----
    const char* mods[] = { "boom", "shift", "space", "comp", "sat", "exp", "more" };
    for (auto* m : mods)
    {
        for (int target = 0; target < 3; ++target)
        {
            PunchLabAudioProcessor p;
            p.prepareToPlay (sr, 512);
            setParam (p, juce::String (m) + "_on", 1);
            setParam (p, juce::String (m) + "_amount", 1.0f);
            setParam (p, juce::String (m) + "_target", (float) target);
            if (target == 2) { setParam (p, juce::String (m) + "_focus", 1); setParam (p, juce::String (m) + "_focus_lo", 200); setParam (p, juce::String (m) + "_focus_hi", 5000); }
            if (juce::String (m) == "shift") setParam (p, "shift_pitch", 7);
            auto s = render (p, loop);
            const bool ok = s.finite && s.peak < 8.0f && s.rms > 0.01f;
            std::printf ("[module %-5s target %d%s] peak %.3f rms %.3f %s\n", m, target, target == 2 ? "+focus" : "      ", s.peak, s.rms, ok ? "OK" : "FAIL");
            if (! ok) ++failures;
        }
    }

    // ---- 5. режимы ----
    struct ModeTest { const char* on; const char* param; int count; };
    const ModeTest modes[] = { { "space_on", "space_type", 5 }, { "comp_on", "comp_mode", 6 }, { "sat_on", "sat_mode", 6 }, { "exp_on", "exp_mode", 6 } };
    for (auto& mt : modes)
        for (int k = 0; k < mt.count; ++k)
        {
            PunchLabAudioProcessor p;
            p.prepareToPlay (sr, 512);
            const juce::String pre = juce::String (mt.on).upToFirstOccurrenceOf ("_", false, false);
            setParam (p, mt.on, 1); setParam (p, pre + "_amount", 0.8f); setParam (p, mt.param, (float) k);
            auto s = render (p, loop);
            const bool ok = s.finite && s.peak < 8.0f && s.rms > 0.005f;
            std::printf ("[%s=%d] peak %.3f rms %.3f %s\n", mt.param, k, s.peak, s.rms, ok ? "OK" : "FAIL");
            if (! ok) ++failures;
        }

    // ---- 6. заводские пресеты + loudness match ----
    {
        PunchLabAudioProcessor p;
        p.prepareToPlay (sr, 512);
        for (int i = 0; i < p.getNumPrograms(); ++i)
        {
            p.setCurrentProgram (i);
            p.prepareToPlay (sr, 512);
            auto s = render (p, loop);
            setParam (p, "loudmatch", 1);
            p.prepareToPlay (sr, 512);
            auto s2 = render (p, loop);
            setParam (p, "loudmatch", 0);
            const bool ok = s.finite && s2.finite && s.peak < 8.0f;
            std::printf ("[preset %2d %-28s] peak %.3f rms %.3f | LM rms %.3f %s\n", i, p.getProgramName (i).toRawUTF8(), s.peak, s.rms, s2.rms, ok ? "OK" : "FAIL");
            if (! ok) ++failures;
        }
    }

    // ---- 7. смена порядка и состояние ----
    {
        PunchLabAudioProcessor p;
        p.prepareToPlay (sr, 512);
        p.moveModuleToSlot (pl::kMore, 0);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        PunchLabAudioProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        const bool ok = q.getModuleOrder() == p.getModuleOrder() && q.getModuleOrder()[0] == pl::kMore;
        std::printf ("[state/order roundtrip] %s (%s)\n", ok ? "OK" : "FAIL", pl::orderToString (q.getModuleOrder()).toRawUTF8());
        if (! ok) ++failures;
    }

    // ---- 8. нестандартные размеры блоков и частоты ----
    for (double rate : { 44100.0, 96000.0 })
    {
        PunchLabAudioProcessor p;
        p.prepareToPlay (rate, 256);
        p.setCurrentProgram (31 < p.getNumPrograms() ? p.getNumPrograms() - 1 : 0);
        p.prepareToPlay (rate, 256);
        auto l = makeLoop (rate, 2.0);
        auto s = render (p, l, nullptr, 1000); // блоки больше заявленного
        const bool ok = s.finite && s.peak < 8.0f;
        std::printf ("[sr %.0f, oversize blocks] peak %.3f latency %d %s\n", rate, s.peak, p.getLatencySamples(), ok ? "OK" : "FAIL");
        if (! ok) ++failures;
    }

    std::printf ("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
