#include "PluginEditor.h"

// ── Couleurs globales ─────────────────────────────────────────────────────────
static const juce::Colour kBG        { 0xff0d0e11 };
static const juce::Colour kPanel     { 0xff1a1c21 };
static const juce::Colour kBorder    { 0xff2a2d35 };
static const juce::Colour kGold      { 0xffd4af37 };
static const juce::Colour kRed       { 0xffcc2222 };
static const juce::Colour kGreen     { 0xff22cc66 };
static const juce::Colour kText      { 0xffcccccc };
static const juce::Colour kTextDim   { 0xff666666 };

// ==============================================================================
// NADA LOOK AND FEEL
// ==============================================================================
NADALookAndFeel::NADALookAndFeel()
{
    setColour(juce::Slider::thumbColourId,              kRed);
    setColour(juce::Slider::rotarySliderFillColourId,   kGold);
    setColour(juce::Slider::rotarySliderOutlineColourId, kBorder);
    setColour(juce::Label::textColourId,                 kText);
    setColour(juce::TextButton::buttonColourId,          kPanel);
    setColour(juce::TextButton::textColourOffId,         kGold);
}

void NADALookAndFeel::drawRotarySlider(juce::Graphics& g,
                                       int x, int y, int width, int height,
                                       float sliderPos,
                                       float rotaryStartAngle, float rotaryEndAngle,
                                       juce::Slider&)
{
    auto bounds  = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(6);
    auto centre  = bounds.getCentre();
    auto radius  = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    auto angle   = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Ombre
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillEllipse(centre.x - radius + 3, centre.y - radius + 5, radius * 2.0f, radius * 2.0f);

    // Corps du knob
    juce::ColourGradient grad(juce::Colour(0xff3a3d47), centre.x, centre.y - radius,
                               juce::Colour(0xff14161a), centre.x, centre.y + radius, false);
    grad.addColour(0.4, juce::Colour(0xff252830));
    g.setGradientFill(grad);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);

    // Anneau extérieur
    g.setColour(kBorder);
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

    // Arc de progression
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f,
                      0.0f, rotaryStartAngle, angle, true);
    g.setColour(kGold.withAlpha(0.8f));
    g.strokePath(arc, juce::PathStrokeType(2.0f));

    // Pointeur
    juce::Path pointer;
    auto pLen = radius * 0.55f;
    pointer.addRoundedRectangle(-2.0f, -radius + 4.0f, 4.0f, pLen, 2.0f);
    pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(centre));
    g.setColour(kRed);
    g.fillPath(pointer);

    // Highlight
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.drawEllipse(centre.x - radius * 0.6f, centre.y - radius * 0.6f,
                  radius * 0.8f, radius * 0.5f, 1.0f);
}

void NADALookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& bg,
                                            bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    if (isDown) bounds = bounds.translated(0.0f, 1.0f);

    bool isAI = button.getName().startsWith("AI");

    if (isAI)
    {
        juce::ColourGradient grad(juce::Colour(0xffffd700), bounds.getTopLeft(),
                                   juce::Colour(0xff8b6914), bounds.getBottomRight(), false);
        g.setGradientFill(grad);
    }
    else
    {
        g.setColour(isHighlighted ? kPanel.brighter(0.15f) : kPanel);
    }
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(isAI ? kGold.withAlpha(0.4f) : kBorder);
    g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
}

void NADALookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.setColour(label.findColour(juce::Label::backgroundColourId));
    g.fillRoundedRectangle(label.getLocalBounds().toFloat(), 2.0f);
    g.setColour(label.findColour(juce::Label::textColourId));
    g.setFont(juce::Font("Consolas", label.getFont().getHeight(), juce::Font::plain));
    g.drawFittedText(label.getText(), label.getLocalBounds(), label.getJustificationType(), 1);
}

// ==============================================================================
// SPECTRUM DISPLAY
// ==============================================================================
void SpectrumDisplay::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(kBG);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    const float values[6] = {
        profile.lowFreqEnergy,    profile.lowMidEnergy,
        profile.midEnergy,        profile.presenceEnergy,
        profile.sibilanceEnergy,  profile.brillianceEnergy
    };

    float barW   = (bounds.getWidth() - 10.0f) / 6.0f - 4.0f;
    float maxH   = bounds.getHeight() - 30.0f;
    float startX = bounds.getX() + 5.0f;

    for (int i = 0; i < 6; ++i)
    {
        float barH = juce::jlimit(2.0f, maxH, values[i] * maxH);
        float bx   = startX + i * (barW + 4.0f);
        float by   = bounds.getBottom() - 20.0f - barH;

        // Background bar
        g.setColour(juce::Colour(0xff222428));
        g.fillRect(bx, bounds.getBottom() - 20.0f - maxH, barW, maxH);

        // Filled bar
        juce::ColourGradient barGrad(bandColours[i].withAlpha(0.9f), bx, by,
                                      bandColours[i].withAlpha(0.4f), bx, by + barH, false);
        g.setGradientFill(barGrad);
        g.fillRect(bx, by, barW, barH);

        // Label
        g.setColour(kTextDim);
        g.setFont(juce::Font("Consolas", 7.0f, juce::Font::plain));
        g.drawText(bandNames[i], (int)bx - 2, (int)(bounds.getBottom() - 18), (int)(barW + 4), 14,
                   juce::Justification::centred);
    }

    // Dynamic range indicator
    g.setColour(kGold.withAlpha(0.7f));
    g.setFont(juce::Font("Consolas", 8.0f, juce::Font::plain));
    g.drawText("DR: " + juce::String(profile.dynamicRange, 1) + " dB",
               bounds.getX() + 4, bounds.getY() + 4, 80, 12, juce::Justification::left);
}

// ==============================================================================
// KNOB SECTION
// ==============================================================================
KnobSection::KnobSection(const juce::String& title,
                          NADAAudioProcessor& proc,
                          const juce::StringArray& paramIds,
                          const juce::StringArray& labels,
                          NADALookAndFeel& lnf)
    : sectionTitle(title)
{
    for (int i = 0; i < paramIds.size(); ++i)
    {
        auto* knob = knobs.add(new juce::Slider());
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
        knob->setColour(juce::Slider::textBoxTextColourId, kTextDim);
        knob->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob->setLookAndFeel(&lnf);
        addAndMakeVisible(knob);

        auto* lbl = knobLabels.add(new juce::Label());
        lbl->setText(labels[i], juce::dontSendNotification);
        lbl->setFont(juce::Font("Consolas", 9.0f, juce::Font::plain));
        lbl->setColour(juce::Label::textColourId, kGold.withAlpha(0.8f));
        lbl->setJustificationType(juce::Justification::centred);
        addAndMakeVisible(lbl);

        attachments.add(new juce::AudioProcessorValueTreeState::SliderAttachment(
            proc.apvts, paramIds[i], *knob));
    }
}

void KnobSection::resized()
{
    auto area   = getLocalBounds().reduced(8, 24);
    int  n      = knobs.size();
    if (n == 0) return;

    int knobW = area.getWidth() / n;
    for (int i = 0; i < n; ++i)
    {
        auto cell = area.removeFromLeft(knobW);
        knobLabels[i]->setBounds(cell.removeFromBottom(14));
        knobs[i]->setBounds(cell.reduced(4));
    }
}

void KnobSection::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(kPanel);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    g.setColour(kGold.withAlpha(0.6f));
    g.setFont(juce::Font("Consolas", 9.0f, juce::Font::bold));
    g.drawText(sectionTitle, getLocalBounds().removeFromTop(20), juce::Justification::centred);

    // Ligne séparatrice sous le titre
    g.setColour(kBorder);
    g.drawLine(10.0f, 20.0f, (float)getWidth() - 10.0f, 20.0f, 1.0f);
}

// ==============================================================================
// EDITOR
// ==============================================================================
NADAAudioProcessorEditor::NADAAudioProcessorEditor(NADAAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setLookAndFeel(&lnf);
    buildSections();

    // Spectrum display
    addAndMakeVisible(spectrumDisplay);

    // AI Buttons
    aiAnalyzeBtn.setButtonText("NADA ANALYZE");
    aiAnalyzeBtn.setName("AI_ANALYZE");
    aiAnalyzeBtn.onClick = [this] { audioProcessor.triggerNADAAnalysis(); };
    addAndMakeVisible(aiAnalyzeBtn);

    aiAutoBtn.setButtonText("AUTO");
    addAndMakeVisible(aiAutoBtn);
    aiAutoBtn.setColour(juce::ToggleButton::textColourId, kText);

    aiStatusLabel.setText("NADA BOSS // READY", juce::dontSendNotification);
    aiStatusLabel.setFont(juce::Font("Consolas", 10.0f, juce::Font::bold));
    aiStatusLabel.setColour(juce::Label::textColourId, kGreen);
    aiStatusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(aiStatusLabel);

    // Master Limiter knob
    masterKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    masterKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
    masterKnob.setColour(juce::Slider::textBoxTextColourId, kGold);
    masterKnob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(masterKnob);
    masterAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        p.apvts, "LIMITER_THRESH", masterKnob);

    startTimerHz(30);
    setSize(1400, 700);
    setResizable(true, true);
}

void NADAAudioProcessorEditor::buildSections()
{
    pitchSection = std::make_unique<KnobSection>(
        "// PITCH & TUNE",
        audioProcessor,
        juce::StringArray { "AUTOTUNE_PITCH", "AUTOTUNE_AMOUNT", "AUTOTUNE_SPEED", "AUTOTUNE_HUMAN" },
        juce::StringArray { "PITCH", "AMOUNT", "SPEED", "HUMAN" },
        lnf);
    addAndMakeVisible(*pitchSection);

    compSection = std::make_unique<KnobSection>(
        "// DYNAMICS",
        audioProcessor,
        juce::StringArray { "FET_THRESH", "FET_RATIO", "FET_ATTACK", "FET_RELEASE",
                            "OPTO_RED", "RVOX_COMP", "DEESSER_RANGE" },
        juce::StringArray { "THRESH", "RATIO", "ATTACK", "RELEASE",
                            "OPTO RED", "R-VOX", "DE-ESS" },
        lnf);
    addAndMakeVisible(*compSection);

    colorSection = std::make_unique<KnobSection>(
        "// COLOUR",
        audioProcessor,
        juce::StringArray { "PULTEC_LOW_BOOST", "PULTEC_HIGH_BOOST", "SSL_DRIVE", "SAT_DRIVE" },
        juce::StringArray { "LOW", "AIR", "SSL", "WARMTH" },
        lnf);
    addAndMakeVisible(*colorSection);

    fxSection = std::make_unique<KnobSection>(
        "// SPACE",
        audioProcessor,
        juce::StringArray { "REVERB_SIZE", "REVERB_MIX", "DELAY_MIX", "STEREO_WIDTH" },
        juce::StringArray { "ROOM", "REV MIX", "DLY MIX", "WIDTH" },
        lnf);
    addAndMakeVisible(*fxSection);
}

NADAAudioProcessorEditor::~NADAAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void NADAAudioProcessorEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(kBG);

    // Header gradient
    juce::ColourGradient headerGrad(juce::Colour(0xff1e2028), 0, 0,
                                     juce::Colour(0xff14161a), 0, 60, false);
    g.setGradientFill(headerGrad);
    g.fillRect(0, 0, getWidth(), 60);

    // Header border
    g.setColour(kGold.withAlpha(0.3f));
    g.drawLine(0, 60, (float)getWidth(), 60, 1.0f);

    // Logo / Title
    g.setColour(kGold);
    g.setFont(juce::Font("Consolas", 18.0f, juce::Font::bold));
    g.drawText("NADA BOSS", 20, 0, 200, 60, juce::Justification::centredLeft);

    g.setColour(kTextDim);
    g.setFont(juce::Font("Consolas", 9.0f, juce::Font::plain));
    g.drawText("ELITE VOCAL CHANNEL STRIP  //  v1.0", 20, 34, 400, 14, juce::Justification::left);

    // Séparateurs verticaux entre sections
    g.setColour(kBorder.withAlpha(0.5f));
    for (int i = 1; i < 4; ++i)
        g.drawLine((float)(getWidth() / 4 * i), 68.0f, (float)(getWidth() / 4 * i), (float)(getHeight() - 180), 1.0f);

    // Master label
    g.setColour(kGold.withAlpha(0.7f));
    g.setFont(juce::Font("Consolas", 9.0f, juce::Font::bold));
    g.drawText("// MASTER", getWidth() - 130, getHeight() - 175, 120, 16, juce::Justification::centred);
}

void NADAAudioProcessorEditor::resized()
{
    auto area   = getLocalBounds();
    auto header = area.removeFromTop(60);

    // Header items (droite vers gauche)
    auto headerRight = header.removeFromRight(250);
    aiAnalyzeBtn.setBounds(headerRight.removeFromRight(130).reduced(10, 12));
    aiAutoBtn.setBounds   (headerRight.removeFromRight(70).reduced(5, 12));

    // Sections principales
    auto mainArea   = area.removeFromTop(area.getHeight() - 175);
    int  sectionW   = mainArea.getWidth() / 4;

    pitchSection->setBounds (mainArea.removeFromLeft(sectionW).reduced(4));
    compSection->setBounds  (mainArea.removeFromLeft(sectionW).reduced(4));
    colorSection->setBounds (mainArea.removeFromLeft(sectionW).reduced(4));
    fxSection->setBounds    (mainArea.reduced(4));

    // Bottom panel
    auto bottom = area;
    auto aiPanel     = bottom.removeFromLeft(bottom.getWidth() - 150);
    auto masterPanel = bottom;

    // Spectrum + status
    aiStatusLabel.setBounds(aiPanel.removeFromTop(24).reduced(10, 2));
    spectrumDisplay.setBounds(aiPanel.reduced(6));

    // Master knob
    masterKnob.setBounds(masterPanel.reduced(10));
}

void NADAAudioProcessorEditor::timerCallback()
{
    updateAIDisplay();
}

void NADAAudioProcessorEditor::updateAIDisplay()
{
    auto state = audioProcessor.getAIState();
    spectrumDisplay.setProfile(state.lastProfile);

    aiStatusLabel.setText(state.statusText, juce::dontSendNotification);
    aiStatusLabel.setColour(juce::Label::textColourId,
        state.isAnalyzing ? juce::Colour(0xffff9500) : kGreen);
}
