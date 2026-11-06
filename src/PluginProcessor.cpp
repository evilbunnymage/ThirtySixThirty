#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using juce::AudioParameterBool;
using juce::AudioParameterFloat;
using juce::AudioParameterFloatAttributes;
using juce::NormalisableRange;
using juce::ParameterID;

juce::String timeText (float ms)
{
    if (ms >= 1000.0f)
        return juce::String (ms / 1000.0f, 2) + " s";
    if (ms >= 100.0f)
        return juce::String (juce::roundToInt (ms)) + " ms";
    return juce::String (ms, ms < 1.0f ? 2 : 1) + " ms";
}

NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [lo, hi] (float, float, float n) { return lo * std::pow (hi / lo, n); },
             [lo, hi] (float, float, float v) { return std::log (v / lo) / std::log (hi / lo); } };
}
} // namespace

SixThirtyProcessor::SixThirtyProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pThreshold = apvts.getRawParameterValue ("threshold");
    pRatio = apvts.getRawParameterValue ("ratio");
    pAttack = apvts.getRawParameterValue ("attack");
    pRelease = apvts.getRawParameterValue ("release");
    pOutput = apvts.getRawParameterValue ("output");
    pGateThreshold = apvts.getRawParameterValue ("gateThreshold");
    pGateRate = apvts.getRawParameterValue ("gateRate");
    pRms = apvts.getRawParameterValue ("rms");
    pSoftKnee = apvts.getRawParameterValue ("softKnee");
    pMinus10 = apvts.getRawParameterValue ("minus10");
    pLink = apvts.getRawParameterValue ("link");
    pBypass = apvts.getRawParameterValue ("bypass");
    pExtSidechain = apvts.getRawParameterValue ("extSidechain");
}

juce::AudioProcessorValueTreeState::ParameterLayout SixThirtyProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "threshold", 1 }, "Threshold", NormalisableRange<float> (-40.0f, 20.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dBu").withStringFromValueFunction ([] (float v, int) {
            return juce::String (v, 1) + " dBu";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ratio", 1 }, "Ratio", NormalisableRange<float> (1.0f, sixthirty::kInfRatio, 0.01f, 0.5f), 4.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return v >= sixthirty::kInfRatio - 0.05f ? juce::String ("inf:1") : juce::String (v, 1) + ":1";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Attack", logRange (0.1f, 200.0f), 5.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return timeText (v); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release", logRange (50.0f, 3000.0f), 300.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return timeText (v); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-20.0f, 20.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
            return juce::String (v, 1) + " dB";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateThreshold", 1 }, "Gate Threshold", NormalisableRange<float> (-80.0f, -10.0f, 0.1f), -80.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) {
            return v <= sixthirty::kGateOffDbu ? juce::String ("Off") : juce::String (v, 1) + " dBu";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "gateRate", 1 }, "Gate Rate", logRange (20.0f, 2000.0f), 200.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return timeText (v); })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "rms", 1 }, "RMS Mode", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "softKnee", 1 }, "Soft Knee", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "minus10", 1 }, "-10 dBV Level", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "link", 1 }, "Stereo Link", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "extSidechain", 1 }, "External Sidechain", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

sixthirty::Params SixThirtyProcessor::readParams() const
{
    sixthirty::Params p;
    p.thresholdDbu = pThreshold->load();
    p.ratio = pRatio->load();
    p.attackMs = pAttack->load();
    p.releaseMs = pRelease->load();
    p.outputDb = pOutput->load();
    p.gateThresholdDbu = pGateThreshold->load();
    p.gateRateMs = pGateRate->load();
    p.rms = pRms->load() > 0.5f;
    p.softKnee = pSoftKnee->load() > 0.5f;
    p.minus10dBV = pMinus10->load() > 0.5f;
    p.stereoLink = pLink->load() > 0.5f;
    p.bypass = pBypass->load() > 0.5f;
    return p;
}

juce::AudioProcessorParameter* SixThirtyProcessor::getBypassParameter() const
{
    return apvts.getParameter ("bypass");
}

void SixThirtyProcessor::prepareToPlay (double sampleRate, int)
{
    dsp.setParams (readParams());
    dsp.prepare (sampleRate);
}

bool SixThirtyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    const auto sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void SixThirtyProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    dsp.setParams (readParams());

    auto main = getBusBuffer (buffer, true, 0);

    const float* const* scPtrs = nullptr;
    int scChannels = 0;
    juce::AudioBuffer<float> sc;
    if (pExtSidechain->load() > 0.5f && getBusCount (true) > 1 && getBus (true, 1) != nullptr
        && getBus (true, 1)->isEnabled())
    {
        sc = getBusBuffer (buffer, true, 1);
        scPtrs = sc.getArrayOfReadPointers();
        scChannels = sc.getNumChannels();
    }

    dsp.process (main.getArrayOfWritePointers(), main.getNumChannels(), scPtrs, scChannels, main.getNumSamples());

    gainReductionDb.store (dsp.getGainReductionDb());
    gateClosed.store (dsp.isGateClosed());
}

juce::AudioProcessorEditor* SixThirtyProcessor::createEditor()
{
    return new SixThirtyEditor (*this);
}

void SixThirtyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SixThirtyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SixThirtyProcessor();
}
