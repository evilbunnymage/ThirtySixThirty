#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

class SixThirtyEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SixThirtyEditor (SixThirtyProcessor&);
    ~SixThirtyEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    void addKnob (Knob& k, const juce::String& paramId, const juce::String& text);
    void addToggle (Toggle& t, const juce::String& paramId, const juce::String& text);
    void timerCallback() override;
    void updateTimingEnabled();

    SixThirtyProcessor& processor;

    Knob threshold, ratio, attack, release, output, gateThreshold, gateRate;
    Toggle rms, softKnee, minus10, link, extSidechain, bypass;

    float shownGr = 0.0f;
    bool shownGateClosed = false;
    juce::Rectangle<float> meterArea, gateLedArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SixThirtyEditor)
};
