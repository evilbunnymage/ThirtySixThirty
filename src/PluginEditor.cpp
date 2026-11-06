#include "PluginEditor.h"

namespace
{
const juce::Colour panel (0xff1c1e22);
const juce::Colour panelEdge (0xff2c3036);
const juce::Colour amber (0xffffb02e);
const juce::Colour dim (0xff3a3326);
const juce::Colour green (0xff4cd964);
const juce::Colour red (0xffff453a);
const juce::Colour text (0xffd7d9dc);
} // namespace

SixThirtyEditor::SixThirtyEditor (SixThirtyProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    addKnob (threshold, "threshold", "THRESHOLD");
    addKnob (ratio, "ratio", "RATIO");
    addKnob (attack, "attack", "ATTACK");
    addKnob (release, "release", "RELEASE");
    addKnob (output, "output", "OUTPUT");
    addKnob (gateThreshold, "gateThreshold", "GATE THRESH");
    addKnob (gateRate, "gateRate", "GATE RATE");

    addToggle (rms, "rms", "RMS");
    addToggle (softKnee, "softKnee", "SOFT KNEE");
    addToggle (minus10, "minus10", "-10 dBV");
    addToggle (link, "link", "LINK");
    addToggle (extSidechain, "extSidechain", "EXT SC");
    addToggle (bypass, "bypass", "BYPASS");

    rms.button.setTooltip ("Off = peak detector. On = RMS detector (attack/release become program dependent).");
    minus10.button.setTooltip ("Off = +4 dBu reference. On = -10 dBV: the detector is driven 11.8 dB harder.");

    rms.button.onClick = [this] { updateTimingEnabled(); };
    updateTimingEnabled();

    setSize (760, 360);
    startTimerHz (30);
}

SixThirtyEditor::~SixThirtyEditor()
{
    stopTimer();
}

void SixThirtyEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& labelText)
{
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, amber);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, panelEdge);
    k.slider.setColour (juce::Slider::thumbColourId, text);
    k.slider.setColour (juce::Slider::textBoxTextColourId, text);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setDoubleClickReturnValue (true, processor.apvts.getParameter (paramId)->getDefaultValue()
                                                  * (processor.apvts.getParameterRange (paramId).end
                                                     - processor.apvts.getParameterRange (paramId).start)
                                                  + processor.apvts.getParameterRange (paramId).start);
    addAndMakeVisible (k.slider);

    k.label.setText (labelText, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, text);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
}

void SixThirtyEditor::addToggle (Toggle& t, const juce::String& paramId, const juce::String& buttonText)
{
    t.button.setButtonText (buttonText);
    t.button.setClickingTogglesState (true);
    t.button.setColour (juce::TextButton::buttonColourId, panelEdge);
    t.button.setColour (juce::TextButton::buttonOnColourId, amber);
    t.button.setColour (juce::TextButton::textColourOffId, text);
    t.button.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    addAndMakeVisible (t.button);
    t.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, paramId, t.button);
}

void SixThirtyEditor::updateTimingEnabled()
{
    // Mirrors the hardware: attack/release are inactive in RMS mode.
    const bool peakMode = ! rms.button.getToggleState();
    attack.slider.setEnabled (peakMode);
    release.slider.setEnabled (peakMode);
    attack.label.setAlpha (peakMode ? 1.0f : 0.4f);
    release.label.setAlpha (peakMode ? 1.0f : 0.4f);
}

void SixThirtyEditor::timerCallback()
{
    const float gr = processor.gainReductionDb.load();
    const bool closed = processor.gateClosed.load();
    // quick fall-off so the meter is readable
    shownGr = gr > shownGr ? gr : shownGr * 0.85f + gr * 0.15f;
    if (shownGr < 0.05f)
        shownGr = 0.0f;
    shownGateClosed = closed;
    updateTimingEnabled();
    repaint (meterArea.getSmallestIntegerContainer().expanded (2));
    repaint (gateLedArea.getSmallestIntegerContainer().expanded (2));
}

void SixThirtyEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel);
    g.setColour (panelEdge);
    g.drawRect (getLocalBounds(), 1);

    g.setColour (amber);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SIXTHIRTY", 20, 10, 200, 26, juce::Justification::centredLeft);
    g.setColour (text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("3630-style compressor / limiter / gate  -  unofficial homage", 20, 34, 400, 16, juce::Justification::centredLeft);

    // Gain-reduction meter: 12 segments, 1..30 dB like the hardware
    g.setColour (text);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("GAIN REDUCTION", meterArea.getX() - 25, meterArea.getY() - 20, meterArea.getWidth() + 50, 14,
                juce::Justification::centred);

    const int segs = 12;
    const float segH = meterArea.getHeight() / (float) segs;
    for (int i = 0; i < segs; ++i)
    {
        const float db = 1.0f + (float) i * (29.0f / (float) (segs - 1)); // 1 .. 30 dB, top to bottom
        const bool lit = shownGr >= db - 0.5f;
        const auto r = juce::Rectangle<float> (meterArea.getX(), meterArea.getY() + (float) i * segH + 1.0f,
                                               meterArea.getWidth(), segH - 2.0f);
        g.setColour (lit ? amber : dim);
        g.fillRoundedRectangle (r, 2.0f);
    }
    g.setColour (text.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("-1", (int) meterArea.getRight() + 4, (int) meterArea.getY(), 30, 12, juce::Justification::centredLeft);
    g.drawText ("-30", (int) meterArea.getRight() + 4, (int) meterArea.getBottom() - 12, 30, 12, juce::Justification::centredLeft);

    // Gate LEDs
    g.setColour (shownGateClosed ? dim : green);
    g.fillEllipse (gateLedArea.getX(), gateLedArea.getY(), 12.0f, 12.0f);
    g.setColour (shownGateClosed ? red : dim);
    g.fillEllipse (gateLedArea.getX() + 38.0f, gateLedArea.getY(), 12.0f, 12.0f);
    g.setColour (text.withAlpha (0.7f));
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("OPEN", (int) gateLedArea.getX() - 8, (int) gateLedArea.getY() + 14, 30, 12, juce::Justification::centred);
    g.drawText ("CLOSE", (int) gateLedArea.getX() + 30, (int) gateLedArea.getY() + 14, 34, 12, juce::Justification::centred);
}

void SixThirtyEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (46);

    // right column: meter + switches
    auto right = area.removeFromRight (150);
    right.removeFromTop (22);
    meterArea = juce::Rectangle<float> ((float) right.getX() + 20.0f, (float) right.getY(), 36.0f, 150.0f);
    auto switches = right.withTrimmedTop (170);
    const int bh = 26;
    auto row = [&] (Toggle& a, Toggle* b)
    {
        auto r = switches.removeFromTop (bh);
        if (b != nullptr)
        {
            a.button.setBounds (r.removeFromLeft (r.getWidth() / 2 - 2));
            r.removeFromLeft (4);
            b->button.setBounds (r);
        }
        else
        {
            a.button.setBounds (r);
        }
        switches.removeFromTop (4);
    };
    row (rms, &softKnee);
    row (minus10, &link);
    row (extSidechain, &bypass);

    // knobs: two rows
    const int top = area.getHeight() / 2;
    auto row1 = area.removeFromTop (top);
    auto row2 = area;

    auto layoutRow = [] (juce::Rectangle<int> r, std::initializer_list<Knob*> knobs)
    {
        const int w = r.getWidth() / 5;
        for (auto* k : knobs)
        {
            auto cell = r.removeFromLeft (w);
            k->label.setBounds (cell.removeFromTop (16));
            k->slider.setBounds (cell);
        }
    };
    layoutRow (row1, { &threshold, &ratio, &attack, &release, &output });
    layoutRow (row2, { &gateThreshold, &gateRate });

    gateLedArea = juce::Rectangle<float> ((float) row2.getX() + (float) row2.getWidth() / 5.0f * 2.0f + 70.0f,
                                          (float) row2.getY() + 40.0f, 50.0f, 12.0f);
}
