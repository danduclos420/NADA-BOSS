#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/PluginProcessor.h"

// ==============================================================================
// NADA LOOK AND FEEL
// ==============================================================================
class NADALookAndFeel : public juce::LookAndFeel_V4
{
public:
    NADALookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override;

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                               const juce::Colour& bg,
                               bool isHighlighted, bool isDown) override;

    void drawLabel(juce::Graphics& g, juce::Label& label) override;
};

// ==============================================================================
// SPECTRUM DISPLAY COMPONENT
// ==============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    void setProfile(const AISpectralAnalyzer::VocalProfile& p) { profile = p; repaint(); }
    void paint(juce::Graphics& g) override;

private:
    AISpectralAnalyzer::VocalProfile profile;

    const juce::String bandNames[6]  = { "RUMBLE", "BODY", "MID", "PRESENCE", "SIBILANCE", "AIR" };
    const juce::Colour bandColours[6] = {
        juce::Colour(0xff880000), juce::Colour(0xff994400),
        juce::Colour(0xff998800), juce::Colour(0xff008844),
        juce::Colour(0xff0066cc), juce::Colour(0xff6600cc)
    };
};

// ==============================================================================
// KNOB SECTION
// ==============================================================================
class KnobSection : public juce::Component
{
public:
    KnobSection(const juce::String& title,
                NADAAudioProcessor& proc,
                const juce::StringArray& paramIds,
                const juce::StringArray& labels,
                NADALookAndFeel& lnf);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    juce::String sectionTitle;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::Label>  knobLabels;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> attachments;
};

// ==============================================================================
// NADA AUDIO PROCESSOR EDITOR
// ==============================================================================
class NADAAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  public juce::Timer
{
public:
    NADAAudioProcessorEditor(NADAAudioProcessor&);
    ~NADAAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    NADAAudioProcessor& audioProcessor;
    NADALookAndFeel     lnf;

    std::unique_ptr<KnobSection> pitchSection;
    std::unique_ptr<KnobSection> compSection;
    std::unique_ptr<KnobSection> colorSection;
    std::unique_ptr<KnobSection> fxSection;

    SpectrumDisplay    spectrumDisplay;
    juce::TextButton   aiAnalyzeBtn;
    juce::ToggleButton aiAutoBtn;
    juce::Label        aiStatusLabel;

    juce::Slider masterKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttach;

    void buildSections();
    void updateAIDisplay();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NADAAudioProcessorEditor)
};