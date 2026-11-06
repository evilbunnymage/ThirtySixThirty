#pragma once

#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

#include "CompressorDSP.h"

class SixThirtyProcessor : public juce::AudioProcessor
{
public:
    SixThirtyProcessor();
    ~SixThirtyProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SixThirty"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState apvts;

    // Read by the editor's meters.
    std::atomic<float> gainReductionDb { 0.0f };
    std::atomic<bool> gateClosed { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    sixthirty::Params readParams() const;

    sixthirty::Compressor dsp;

    std::atomic<float>* pThreshold = nullptr;
    std::atomic<float>* pRatio = nullptr;
    std::atomic<float>* pAttack = nullptr;
    std::atomic<float>* pRelease = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pGateThreshold = nullptr;
    std::atomic<float>* pGateRate = nullptr;
    std::atomic<float>* pRms = nullptr;
    std::atomic<float>* pSoftKnee = nullptr;
    std::atomic<float>* pMinus10 = nullptr;
    std::atomic<float>* pLink = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pExtSidechain = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SixThirtyProcessor)
};
