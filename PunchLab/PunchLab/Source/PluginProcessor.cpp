#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace pl;

static juce::uint32 packOrder (const ModuleOrder& o)
{
    juce::uint32 v = 0;
    for (int i = 0; i < kNumModules; ++i) v |= (juce::uint32) o[(size_t) i] << (3 * i);
    return v;
}

static ModuleOrder unpackOrder (juce::uint32 v)
{
    ModuleOrder o {};
    for (int i = 0; i < kNumModules; ++i) o[(size_t) i] = (int) ((v >> (3 * i)) & 7u);
    return o;
}

PunchLabAudioProcessor::PunchLabAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PunchLabState", createParameterLayout())
{
    auto comp = std::make_unique<Compress>();
    compressor = comp.get();

    slots[kBoom]         = std::make_unique<ModuleSlot> (kBoom,         std::make_unique<BoomShack>());
    slots[kShift]        = std::make_unique<ModuleSlot> (kShift,        std::make_unique<Shift>());
    slots[kSpace]        = std::make_unique<ModuleSlot> (kSpace,        std::make_unique<Space>());
    slots[kCompress]     = std::make_unique<ModuleSlot> (kCompress,     std::move (comp));
    slots[kSaturate]     = std::make_unique<ModuleSlot> (kSaturate,     std::make_unique<Saturate>());
    slots[kExperimental] = std::make_unique<ModuleSlot> (kExperimental, std::make_unique<Experimental>());
    slots[kMore]         = std::make_unique<ModuleSlot> (kMore,         std::make_unique<More>());

    for (auto& s : slots) s->bind (apvts);
    master.bind (apvts);

    pMagnitude = apvts.getRawParameterValue (ID::magnitude);
    pMix       = apvts.getRawParameterValue (ID::mix);
    pOutput    = apvts.getRawParameterValue (ID::output);
    pBypass    = apvts.getRawParameterValue (ID::bypass);
    pLoud      = apvts.getRawParameterValue (ID::loudMatch);
    pSplitTime = apvts.getRawParameterValue (ID::splitTime);
    pSplitSens = apvts.getRawParameterValue (ID::splitSens);
    bypassParam = apvts.getParameter (ID::bypass);

    packedOrder.store (packOrder (defaultModuleOrder()));

    presets = std::make_unique<PresetManager> (apvts,
        [this] { return getModuleOrder(); },
        [this] (const ModuleOrder& o) { setModuleOrder (o); });
}

bool PunchLabAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void PunchLabAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlock = juce::jmax (32, samplesPerBlock);

    int latency = 0;
    for (auto& s : slots) { s->prepare (sampleRate, maxBlock); s->reset(); latency += s->getLatency(); }
    master.prepare (sampleRate);
    loudness.prepare (sampleRate);
    dryDelay.prepare (latency, maxBlock);
    work.setSize (2, maxBlock, false, true, true);
    dry.setSize (2, maxBlock, false, true, true);

    mixSm.prepare (sampleRate, 30.0f);    mixSm.reset (pMix->load());
    outSm.prepare (sampleRate, 30.0f);    outSm.reset (dbToGain (pOutput->load()));
    bypassSm.prepare (sampleRate, 20.0f); bypassSm.reset (pBypass->load() > 0.5f ? 1.0f : 0.0f);

    setLatencySamples (latency);
}

// ============================================================================
void PunchLabAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, n);
    if (n == 0 || buffer.getNumChannels() == 0) return;

    ProcessContext ctx;
    ctx.sampleRate = currentSampleRate;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) ctx.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) ctx.ppq = *ppq;
            ctx.playing = pos->getIsPlaying();
        }
    }

    int offset = 0;
    while (offset < n)
    {
        const int len = juce::jmin (maxBlock, n - offset);
        processChunk (buffer, offset, len, ctx);
        ctx.ppq += (double) len * ctx.bpm / 60.0 / currentSampleRate;
        offset += len;
    }
}

void PunchLabAudioProcessor::processChunk (juce::AudioBuffer<float>& buffer, int offset, int len, const ProcessContext& ctx)
{
    const int numCh = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    float* L = work.getWritePointer (0);
    float* R = work.getWritePointer (1);
    float* dL = dry.getWritePointer (0);
    float* dR = dry.getWritePointer (1);

    const float* inL = buffer.getReadPointer (0, offset);
    const float* inR = numCh > 1 ? buffer.getReadPointer (1, offset) : inL;
    juce::FloatVectorOperations::copy (L, inL, len);
    juce::FloatVectorOperations::copy (R, inR, len);
    juce::FloatVectorOperations::copy (dL, inL, len);
    juce::FloatVectorOperations::copy (dR, inR, len);

    // ---- входной уровень ----
    {
        float pk = 0.0f;
        for (int i = 0; i < len; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        inLevel.store (std::max (pk, inLevel.load() * 0.9f));
    }

    const float magnitude = pMagnitude->load();
    const float splitMs = pSplitTime->load(), splitSens = pSplitSens->load();

    // ---- цепочка модулей ----
    const ModuleOrder order = unpackOrder (packedOrder.load());
    for (int m : order)
        slots[(size_t) m]->process (L, R, len, magnitude, splitMs, splitSens, ctx);

    // ---- мастер ----
    master.process (L, R, len, magnitude);

    // ---- dry/wet, выход, loudness match, bypass ----
    dryDelay.process (dL, dR, len);  // выравнивание сухого сигнала по латентности цепочки

    mixSm.setTarget (pMix->load());
    outSm.setTarget (dbToGain (pOutput->load()));
    for (int i = 0; i < len; ++i)
    {
        const float m = mixSm.next(), g = outSm.next();
        L[i] = (dL[i] + m * (L[i] - dL[i])) * g;
        R[i] = (dR[i] + m * (R[i] - dR[i])) * g;
    }

    loudness.process (dL, dR, L, R, len, pLoud->load() > 0.5f);

    bypassSm.setTarget (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    if (bypassSm.isSmoothing() || bypassSm.current > 0.0f)
    {
        for (int i = 0; i < len; ++i)
        {
            const float b = bypassSm.next();
            L[i] += b * (dL[i] - L[i]);
            R[i] += b * (dR[i] - R[i]);
        }
    }

    // защита от NaN/Inf (на случай экстремальных настроек)
    float pk = 0.0f;
    for (int i = 0; i < len; ++i)
    {
        if (! std::isfinite (L[i])) L[i] = 0.0f;
        if (! std::isfinite (R[i])) R[i] = 0.0f;
        pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    }
    outLevel.store (std::max (pk, outLevel.load() * 0.9f));

    if (numCh > 1)
    {
        juce::FloatVectorOperations::copy (buffer.getWritePointer (0, offset), L, len);
        juce::FloatVectorOperations::copy (buffer.getWritePointer (1, offset), R, len);
    }
    else
    {
        float* out = buffer.getWritePointer (0, offset);
        for (int i = 0; i < len; ++i) out[i] = 0.5f * (L[i] + R[i]);
    }
}

// ============================================================================
ModuleOrder PunchLabAudioProcessor::getModuleOrder() const { return unpackOrder (packedOrder.load()); }

void PunchLabAudioProcessor::setModuleOrder (const ModuleOrder& o)
{
    if (! isValidOrder (o)) return;
    packedOrder.store (packOrder (o));
    ++orderVersion;
}

void PunchLabAudioProcessor::moveModuleToSlot (int moduleId, int slot)
{
    auto o = getModuleOrder();
    auto it = std::find (o.begin(), o.end(), moduleId);
    if (it == o.end() || slot < 0 || slot >= kNumModules) return;
    std::vector<int> v (o.begin(), o.end());
    v.erase (v.begin() + (it - o.begin()));
    v.insert (v.begin() + slot, moduleId);
    std::copy (v.begin(), v.end(), o.begin());
    setModuleOrder (o);
}

// ============================================================================
int PunchLabAudioProcessor::getNumPrograms()   { return (int) getFactoryPresets().size(); }
int PunchLabAudioProcessor::getCurrentProgram()
{
    const int i = presets->getCurrentIndex();
    return i < getNumPrograms() ? i : 0;
}
void PunchLabAudioProcessor::setCurrentProgram (int index)
{
    if (index >= 0 && index < getNumPrograms()) presets->load (index);
}
const juce::String PunchLabAudioProcessor::getProgramName (int index)
{
    const auto& fp = getFactoryPresets();
    return index >= 0 && index < (int) fp.size() ? fp[(size_t) index].name : juce::String();
}

void PunchLabAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("order", orderToString (getModuleOrder()), nullptr);
    juce::String locks;
    for (auto& l : presets->locks) locks << (l.load() ? "1" : "0");
    state.setProperty ("locks", locks, nullptr);
    state.setProperty ("presetName", presets->getCurrentName(), nullptr);
    state.setProperty ("presetIndex", presets->getCurrentIndex(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PunchLabAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml (*xml);
    apvts.replaceState (state);

    // APVTS не обновляет параметр, если сохранённое значение совпадает с уже
    // привязанным (например, bool с «промежуточным» значением). Применяем явно.
    for (auto child : state)
    {
        if (! child.hasType ("PARAM")) continue;
        if (auto* p = apvts.getParameter (child.getProperty ("id").toString()))
        {
            const float norm = p->convertTo0to1 ((float) child.getProperty ("value"));
            if (std::abs (p->getValue() - norm) > 1.0e-6f) p->setValueNotifyingHost (norm);
        }
    }
    setModuleOrder (orderFromString (state.getProperty ("order").toString()));
    const auto locks = state.getProperty ("locks").toString();
    for (int i = 0; i < kNumModules; ++i)
        presets->locks[(size_t) i].store (i < locks.length() && locks[i] == '1');
    presets->setCurrentName (state.getProperty ("presetName", "Init").toString(),
                             (int) state.getProperty ("presetIndex", 0));
}

juce::AudioProcessorEditor* PunchLabAudioProcessor::createEditor()
{
    return new PunchLabEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PunchLabAudioProcessor();
}
