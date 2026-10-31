#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "PresetManager.h"
#include "dsp/ModuleSlot.h"
#include "dsp/BoomShack.h"
#include "dsp/Shift.h"
#include "dsp/Space.h"
#include "dsp/Compress.h"
#include "dsp/Saturate.h"
#include "dsp/Experimental.h"
#include "dsp/More.h"
#include "dsp/Master.h"

class PunchLabAudioProcessor : public juce::AudioProcessor
{
public:
    PunchLabAudioProcessor();
    ~PunchLabAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PunchLab"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // ---- порядок модулей ----
    pl::ModuleOrder getModuleOrder() const;
    void setModuleOrder (const pl::ModuleOrder& order);
    void moveModuleToSlot (int moduleId, int slot);
    int getOrderVersion() const { return orderVersion.load(); }

    pl::PresetManager& getPresetManager() { return *presets; }

    // ---- данные для GUI ----
    float getInputLevel() const  { return inLevel.load(); }
    float getOutputLevel() const { return outLevel.load(); }
    float getCompressorGainReduction() const { return compressor != nullptr ? compressor->gainReductionDb.load() : 0.0f; }
    float getLoudnessMatchGain() const { return loudness.currentGainDb.load(); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void processChunk (juce::AudioBuffer<float>& buffer, int offset, int len, const pl::ProcessContext& ctx);

    std::array<std::unique_ptr<pl::ModuleSlot>, pl::kNumModules> slots;
    pl::Compress* compressor = nullptr;
    pl::MasterSection master;
    pl::LoudnessMatch loudness;
    pl::StereoDelay dryDelay;
    juce::AudioBuffer<float> work, dry;
    pl::Smoother mixSm, outSm, bypassSm;

    std::atomic<juce::uint32> packedOrder { 0 };
    std::atomic<int> orderVersion { 0 };
    std::atomic<float> inLevel { 0.0f }, outLevel { 0.0f };

    std::unique_ptr<pl::PresetManager> presets;
    juce::AudioProcessorParameter* bypassParam = nullptr;

    std::atomic<float>* pMagnitude = nullptr; std::atomic<float>* pMix = nullptr; std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pBypass = nullptr; std::atomic<float>* pLoud = nullptr;
    std::atomic<float>* pSplitTime = nullptr; std::atomic<float>* pSplitSens = nullptr;

    double currentSampleRate = 44100.0;
    int maxBlock = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PunchLabAudioProcessor)
};
