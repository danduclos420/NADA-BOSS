#include "PluginProcessor.h"
#include "../ui/PluginEditor.h"

// ==============================================================================
// CONSTRUCTOR / DESTRUCTOR
// ==============================================================================
NADAAudioProcessor::NADAAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    analysisBuffer.resize(2048, 0.0f);
    startTimerHz(30); // Timer UI — safe, pas dans l'audio thread
}

NADAAudioProcessor::~NADAAudioProcessor() {}

// ==============================================================================
// TIMER CALLBACK — Tourne sur le message thread, JAMAIS sur l'audio thread
// ==============================================================================
void NADAAudioProcessor::timerCallback()
{
    if (analysisRequested.load())
    {
        runAIAnalysis();
        analysisRequested.store(false);
    }
}

// ==============================================================================
// PREPARE TO PLAY
// ==============================================================================
void NADAAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    mSampleRate     = sampleRate;
    mSamplesPerBlock = samplesPerBlock;

    juce::dsp::ProcessSpec stereoSpec { sampleRate, (juce::uint32)samplesPerBlock, 2 };
    juce::dsp::ProcessSpec monoSpec   { sampleRate, (juce::uint32)samplesPerBlock, 1 };

    // Pitch
    pitchShifter.prepare(sampleRate, samplesPerBlock);

    // EQ
    for (int i = 0; i < 6; ++i) eq6.bands[i].prepare(stereoSpec);
    pultec.low.prepare(stereoSpec);
    pultec.high.prepare(stereoSpec);
    for (int i = 0; i < 4; ++i) ssl.bands[i].prepare(stereoSpec);

    // Dynamics (mono par sample)
    fet1176.prepare(sampleRate);
    optoLA2A.prepare(sampleRate);
    hg2.prepare(sampleRate);
    rvox.prepare(sampleRate);
    deesser.prepare(sampleRate);

    // Reverb / Limiter
    reverb.prepare(stereoSpec);
    limiter.prepare(stereoSpec);
    limiter.setRelease(200.0f);

    // Delay parallèle
    int maxDelay = (int)(sampleRate * 2.0); // max 2 secondes
    stereoDelay.prepare(sampleRate, maxDelay);
    stereoDelay.setDelayMs(250.0f);
    stereoDelay.feedback = 0.3f;

    // Smoothed values — 50ms de smoothing pour éviter les clicks
    float smoothMs = 50.0f;
    smoothedFetThresh.reset   (sampleRate, smoothMs / 1000.0);
    smoothedFetRatio.reset    (sampleRate, smoothMs / 1000.0);
    smoothedOptoRed.reset     (sampleRate, smoothMs / 1000.0);
    smoothedSatDrive.reset    (sampleRate, smoothMs / 1000.0);
    smoothedRvoxComp.reset    (sampleRate, smoothMs / 1000.0);
    smoothedDeesserRange.reset(sampleRate, smoothMs / 1000.0);
    smoothedWidth.reset       (sampleRate, smoothMs / 1000.0);
    smoothedReverbMix.reset   (sampleRate, smoothMs / 1000.0);
    smoothedDelayMix.reset    (sampleRate, smoothMs / 1000.0);

    // Valeurs initiales
    smoothedFetThresh.setCurrentAndTargetValue   (apvts.getRawParameterValue("FET_THRESH")->load());
    smoothedFetRatio.setCurrentAndTargetValue    (apvts.getRawParameterValue("FET_RATIO")->load());
    smoothedOptoRed.setCurrentAndTargetValue     (apvts.getRawParameterValue("OPTO_RED")->load());
    smoothedSatDrive.setCurrentAndTargetValue    (apvts.getRawParameterValue("SAT_DRIVE")->load());
    smoothedRvoxComp.setCurrentAndTargetValue    (apvts.getRawParameterValue("RVOX_COMP")->load());
    smoothedDeesserRange.setCurrentAndTargetValue(apvts.getRawParameterValue("DEESSER_RANGE")->load());
    smoothedWidth.setCurrentAndTargetValue       (apvts.getRawParameterValue("STEREO_WIDTH")->load());
    smoothedReverbMix.setCurrentAndTargetValue   (apvts.getRawParameterValue("REVERB_MIX")->load());
    smoothedDelayMix.setCurrentAndTargetValue    (apvts.getRawParameterValue("DELAY_MIX")->load());

    // Reset change detection
    for (int i = 0; i < 6; ++i) prevEqValues[i][0] = prevEqValues[i][1] = prevEqValues[i][2] = -9999.0f;
    prevPultecLow = prevPultecHigh = prevSslDrive = -9999.0f;

    // Force update initial
    updateDSPFromParams();

    analysisBuffer.assign(2048, 0.0f);
    analysisBufferPos = 0;
}

void NADAAudioProcessor::releaseResources() {}

// ==============================================================================
// PROCESS BLOCK — Le bon ordre, corrigé
// ==============================================================================
void NADAAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                      juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalIn  = getTotalNumInputChannels();
    auto totalOut = getTotalNumOutputChannels();
    for (int i = totalIn; i < totalOut; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();

    // ── 1. CAPTURE AUDIO POUR L'AI (avant tout traitement) ──────────────────
    {
        auto* inL = buffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i)
        {
            analysisBuffer[(size_t)analysisBufferPos] = inL[i];
            analysisBufferPos = (analysisBufferPos + 1) % (int)analysisBuffer.size();
        }
    }

    // ── 2. UPDATE EQ (seulement si paramètres ont changé) ───────────────────
    updateDSPFromParams();

    // ── 3. MAJ DES SMOOTHED PARAMS ──────────────────────────────────────────
    smoothedFetThresh.setTargetValue   (apvts.getRawParameterValue("FET_THRESH")->load());
    smoothedFetRatio.setTargetValue    (apvts.getRawParameterValue("FET_RATIO")->load());
    smoothedOptoRed.setTargetValue     (apvts.getRawParameterValue("OPTO_RED")->load());
    smoothedSatDrive.setTargetValue    (apvts.getRawParameterValue("SAT_DRIVE")->load());
    smoothedRvoxComp.setTargetValue    (apvts.getRawParameterValue("RVOX_COMP")->load());
    smoothedDeesserRange.setTargetValue(apvts.getRawParameterValue("DEESSER_RANGE")->load());
    smoothedWidth.setTargetValue       (apvts.getRawParameterValue("STEREO_WIDTH")->load());
    smoothedReverbMix.setTargetValue   (apvts.getRawParameterValue("REVERB_MIX")->load());
    smoothedDelayMix.setTargetValue    (apvts.getRawParameterValue("DELAY_MIX")->load());

    // ── 4. PITCH CORRECTION ──────────────────────────────────────────────────
    float userPitch  = apvts.getRawParameterValue("AUTOTUNE_PITCH")->load();
    float tunerAmt   = apvts.getRawParameterValue("AUTOTUNE_AMOUNT")->load();
    if (tunerAmt > 0.01f && std::abs(userPitch) > 0.01f)
    {
        float pitchRatio = std::pow(2.0f, userPitch / 12.0f);
        float blendRatio = 1.0f + (pitchRatio - 1.0f) * tunerAmt;
        pitchShifter.process(buffer, blendRatio);
    }

    // ── 5. EQ (sculpt) ───────────────────────────────────────────────────────
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        for (int i = 0; i < 6; ++i) eq6.bands[i].process(ctx);
        pultec.low.process(ctx);
        pultec.high.process(ctx);
        for (int i = 0; i < 4; ++i) ssl.bands[i].process(ctx);
    }

    // ── 6. DYNAMICS + SATURATION + DE-ESSER (sample-par-sample) ─────────────
    {
        auto* left  = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);

        for (int s = 0; s < numSamples; ++s)
        {
            float fetThr = smoothedFetThresh.getNextValue();
            float fetRat = smoothedFetRatio.getNextValue();
            float optoRed = smoothedOptoRed.getNextValue();
            float satDrv  = smoothedSatDrive.getNextValue();
            float rvoxThr = smoothedRvoxComp.getNextValue();
            float dsRange = smoothedDeesserRange.getNextValue();

            float l = left[s];
            float r = right[s];

            // De-esser EN PREMIER (avant compression pour éviter la pompe)
            l = deesser.process(l, dsRange);
            r = deesser.process(r, dsRange);

            // FET 1176 — compresseur rapide transitoires
            l = fet1176.process(l, fetThr, fetRat);
            r = fet1176.process(r, fetThr, fetRat);

            // LA-2A — compresseur lent glue
            l = optoLA2A.process(l, optoRed);
            r = optoLA2A.process(r, optoRed);

            // HG-2 — chaleur harmonique / saturation
            l = hg2.process(l, satDrv, 0.08f, 0.05f);
            r = hg2.process(r, satDrv, 0.08f, 0.05f);

            // R-VOX — compresseur vocal final
            l = rvox.process(l, rvoxThr, 0.0005f);
            r = rvox.process(r, rvoxThr, 0.0005f);

            left[s]  = l;
            right[s] = r;
        }
    }

    // ── 7. STEREO WIDTH ──────────────────────────────────────────────────────
    // On snap le width pour cette loop (pas besoin de smooth par sample)
    stereoMaker.process(buffer, smoothedWidth.getCurrentValue());

    // ── 8. REVERB (bus parallèle, DRY préservé) ───────────────────────────
    {
        float reverbMix = smoothedReverbMix.getCurrentValue();
        if (reverbMix > 0.001f)
        {
            // Copie dry
            juce::AudioBuffer<float> wetBuf(buffer.getNumChannels(), numSamples);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                wetBuf.copyFrom(ch, 0, buffer, ch, 0, numSamples);

            // Reverb sur la copie wet
            juce::dsp::AudioBlock<float> wetBlock(wetBuf);
            juce::dsp::ProcessContextReplacing<float> wetCtx(wetBlock);
            reverb.process(wetCtx);

            // Mix parallèle : dry + wet
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.addFrom(ch, 0, wetBuf, ch, 0, numSamples, reverbMix);
        }
    }

    // ── 9. DELAY (bus parallèle) ─────────────────────────────────────────────
    {
        float delayMix = smoothedDelayMix.getCurrentValue();
        if (delayMix > 0.001f)
        {
            auto* left  = buffer.getWritePointer(0);
            auto* right = buffer.getWritePointer(1);

            for (int s = 0; s < numSamples; ++s)
            {
                float dryL = left[s];
                float dryR = right[s];

                // Lecture du delay (signal retardé)
                float delL = stereoDelay.readL();
                float delR = stereoDelay.readR();

                // Écriture dans le buffer delay avec feedback
                stereoDelay.writeAndAdvance(dryL, dryR);

                // Mix : dry + delayed wet
                left[s]  = dryL + delL * delayMix;
                right[s] = dryR + delR * delayMix;
            }
        }
    }

    // ── 10. LIMITER (dernier de la chaîne, toujours) ─────────────────────────
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        limiter.process(ctx);
    }
}

// ==============================================================================
// UPDATE DSP — Seulement si paramètre a changé (évite recalc à 44100Hz)
// ==============================================================================
void NADAAudioProcessor::updateDSPFromParams()
{
    // EQ 6 bandes
    for (int i = 0; i < 6; ++i)
    {
        juce::String prefix = "EQ_BAND_" + juce::String(i + 1) + "_";
        float f = apvts.getRawParameterValue(prefix + "FREQ")->load();
        float g = apvts.getRawParameterValue(prefix + "GAIN")->load();
        float q = apvts.getRawParameterValue(prefix + "Q")->load();

        // Changement détecté ?
        if (f == prevEqValues[i][0] && g == prevEqValues[i][1] && q == prevEqValues[i][2])
            continue;

        prevEqValues[i][0] = f;
        prevEqValues[i][1] = g;
        prevEqValues[i][2] = q;

        bool active = apvts.getRawParameterValue(prefix + "ACTIVE")->load() > 0.5f;
        int  type   = (int)apvts.getRawParameterValue(prefix + "TYPE")->load();

        if (!active)
        {
            *eq6.bands[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makeAllPass(mSampleRate, 1000.0f);
        }
        else if (type == 0)
        {
            *eq6.bands[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(
                mSampleRate, juce::jlimit(20.0f, 20000.0f, f), juce::jlimit(0.1f, 10.0f, q));
        }
        else if (type == 2)
        {
            *eq6.bands[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass(
                mSampleRate, juce::jlimit(20.0f, 20000.0f, f), juce::jlimit(0.1f, 10.0f, q));
        }
        else
        {
            *eq6.bands[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                mSampleRate, juce::jlimit(20.0f, 20000.0f, f),
                juce::jlimit(0.1f, 10.0f, q),
                juce::Decibels::decibelsToGain(juce::jlimit(-18.0f, 18.0f, g)));
        }
    }

    // FET attack/release
    float fAtk = apvts.getRawParameterValue("FET_ATTACK")->load();
    float fRel = apvts.getRawParameterValue("FET_RELEASE")->load();
    fet1176.updateCoefficients(fAtk, fRel);

    // Pultec
    float pLow  = apvts.getRawParameterValue("PULTEC_LOW_BOOST")->load();
    float pHigh = apvts.getRawParameterValue("PULTEC_HIGH_BOOST")->load();
    if (pLow != prevPultecLow)
    {
        prevPultecLow = pLow;
        *pultec.low.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
            mSampleRate, 60.0f, 0.7f, juce::Decibels::decibelsToGain(pLow));
    }
    if (pHigh != prevPultecHigh)
    {
        prevPultecHigh = pHigh;
        *pultec.high.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
            mSampleRate, 10000.0f, 0.7f, juce::Decibels::decibelsToGain(pHigh));
    }

    // SSL coloration
    float sslDrive = apvts.getRawParameterValue("SSL_DRIVE")->load();
    if (sslDrive != prevSslDrive)
    {
        prevSslDrive = sslDrive;
        const float freqs[] = { 200.0f, 800.0f, 3200.0f, 12800.0f };
        const float gains[] = { 0.5f, 0.3f, 0.4f, 0.6f }; // gain différent par bande
        for (int i = 0; i < 4; ++i)
        {
            float g = juce::Decibels::decibelsToGain(sslDrive * gains[i] * 6.0f);
            *ssl.bands[i].coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                mSampleRate, freqs[i], 1.0f, g);
        }
    }

    // Limiter threshold
    limiter.setThreshold(apvts.getRawParameterValue("LIMITER_THRESH")->load());

    // Reverb params
    juce::dsp::Reverb::Parameters revP;
    revP.roomSize   = apvts.getRawParameterValue("REVERB_SIZE")->load();
    revP.wetLevel   = 1.0f;  // Le mix est géré dans processBlock en parallèle
    revP.dryLevel   = 0.0f;
    revP.damping    = 0.5f;
    revP.width      = 1.0f;
    revP.freezeMode = 0.0f;
    reverb.setParameters(revP);

    // Delay time
    float delayTimeMs = 250.0f; // TODO: exposer ce paramètre à l'UI
    stereoDelay.setDelayMs(delayTimeMs);
}

// ==============================================================================
// AI ANALYSIS
// ==============================================================================
void NADAAudioProcessor::triggerNADAAnalysis()
{
    if (aiState.isAnalyzing) return;
    aiState.isAnalyzing = true;
    aiState.statusText  = "ANALYZING VOCAL...";
    analysisRequested.store(true);
}

void NADAAudioProcessor::runAIAnalysis()
{
    // Tourne sur le message thread (via timer) — safe
    aiState.statusText = "SPECTRAL ANALYSIS IN PROGRESS...";

    auto profile  = spectralAnalyzer.analyzeBuffer(analysisBuffer, (float)mSampleRate);
    aiState.lastProfile = profile;

    auto mixParams = aiMixer.generateMixingParameters(profile);
    aiState.lastMixParams = mixParams;

    applyAIParams(mixParams);

    aiState.statusText  = "VOCAL OPTIMIZED  //  RADIO READY ✓";
    aiState.isAnalyzing = false;
}

void NADAAudioProcessor::applyAIParams(const AIMixer::MixingParameters& p)
{
    auto setNorm = [&](const juce::String& id, float val, float min, float max)
    {
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, (val - min) / (max - min)));
    };

    for (int i = 0; i < 6; ++i)
    {
        juce::String s = "EQ_BAND_" + juce::String(i + 1);
        setNorm(s + "_FREQ", p.eqBands[i].frequency, 20.0f, 20000.0f);
        setNorm(s + "_GAIN", p.eqBands[i].gain,      -18.0f, 18.0f);
        setNorm(s + "_Q",    p.eqBands[i].q,          0.1f,  10.0f);
    }
    setNorm("FET_THRESH",    p.fet1176Threshold,  -60.0f, 0.0f);
    setNorm("FET_RATIO",     p.fet1176Ratio,        4.0f, 20.0f);
    setNorm("OPTO_RED",      p.optoLA2AReduction,   0.0f, 100.0f);
    setNorm("SAT_DRIVE",     p.hg2Saturation,       0.0f, 1.0f);
    setNorm("DEESSER_RANGE", p.deesserRange,         0.0f, 1.0f);
    setNorm("REVERB_MIX",    p.reverbMix,            0.0f, 1.0f);
    setNorm("DELAY_MIX",     p.delayMix,             0.0f, 1.0f);
}

// ==============================================================================
// PARAMETER LAYOUT
// ==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout NADAAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterBool> ("AI_ENABLED",      "AI Enabled",    false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AUTOTUNE_PITCH",   "Pitch",        -12.0f, 12.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AUTOTUNE_AMOUNT",  "Amount",         0.0f,  1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AUTOTUNE_SPEED",   "Speed",          0.0f,  1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AUTOTUNE_HUMAN",   "Human",          0.0f,  1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("AUTOTUNE_KEY",    "Key",
        juce::StringArray{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("AUTOTUNE_SCALE",  "Scale",
        juce::StringArray{"Maj","Min"}, 0));

    for (int i = 1; i <= 6; ++i)
    {
        juce::String s = "EQ_BAND_" + juce::String(i);
        int type = (i == 1) ? 0 : (i == 6 ? 2 : 1);
        float defaultFreq = 20.0f * std::pow(10.0f, (float)(i - 1) / 5.0f * 3.0f);
        params.push_back(std::make_unique<juce::AudioParameterFloat>(s + "_FREQ",   s + " Freq",  20.0f, 20000.0f, defaultFreq));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(s + "_GAIN",   s + " Gain", -18.0f, 18.0f, 0.0f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(s + "_Q",      s + " Q",     0.1f,  10.0f,  1.0f));
        params.push_back(std::make_unique<juce::AudioParameterBool> (s + "_ACTIVE", s + " Active", true));
        params.push_back(std::make_unique<juce::AudioParameterChoice>(s + "_TYPE",  s + " Type",
            juce::StringArray{"Low Cut","Bell","High Cut"}, type));
    }

    params.push_back(std::make_unique<juce::AudioParameterFloat>("FET_THRESH",      "FET Threshold", -60.0f, 0.0f,    -20.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FET_RATIO",       "FET Ratio",       4.0f, 20.0f,     4.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FET_ATTACK",      "FET Attack",     20.0f, 800.0f,  100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FET_RELEASE",     "FET Release",    50.0f, 1100.0f, 100.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OPTO_RED",        "Peak Red",        0.0f, 100.0f,   30.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("PULTEC_LOW_BOOST","Low Boost",       0.0f, 12.0f,    0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("PULTEC_HIGH_BOOST","High Boost",     0.0f, 12.0f,    0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SSL_DRIVE",       "SSL Drive",       0.0f,  1.0f,    0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("SAT_DRIVE",       "Sat Drive",       0.0f,  1.0f,    0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("RVOX_COMP",       "Vox Comp",      -30.0f,  0.0f,  -10.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DEESSER_RANGE",   "De-Esser",        0.0f,  1.0f,    0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("STEREO_WIDTH",    "Width",           0.0f,  2.0f,    1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("LIMITER_THRESH",  "Limiter",        -24.0f, 0.0f,   -0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("REVERB_MIX",      "Reverb Mix",      0.0f,  1.0f,    0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("REVERB_SIZE",     "Room Size",       0.0f,  1.0f,    0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("DELAY_MIX",       "Delay Mix",       0.0f,  1.0f,    0.0f));

    return { params.begin(), params.end() };
}

// ==============================================================================
// STATE SAVE / LOAD
// ==============================================================================
void NADAAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void NADAAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NADAAudioProcessor();
}
