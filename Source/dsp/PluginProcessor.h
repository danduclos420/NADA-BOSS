#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include "../ai/AIEngine.h"

// ==============================================================================
// FET COMPRESSOR — 1176 Style
// ==============================================================================
class FETCompressor
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        envelope = 0.0f;
        updateCoefficients(0.1f, 100.0f);
    }

    void updateCoefficients(float attackMs, float releaseMs)
    {
        attCoef = std::exp(-1.0f / (attackMs * 0.001f * (float)sampleRate));
        relCoef = std::exp(-1.0f / (releaseMs * 0.001f * (float)sampleRate));
    }

    float process(float in, float thresholdDb, float ratio)
    {
        float thresh = juce::Decibels::decibelsToGain(thresholdDb);
        float absIn  = std::abs(in);
        envelope = (absIn > envelope)
            ? attCoef * envelope + (1.0f - attCoef) * absIn
            : relCoef * envelope + (1.0f - relCoef) * absIn;

        if (envelope > thresh)
        {
            float overDb  = juce::Decibels::gainToDecibels(envelope / (thresh + 1e-9f));
            float gainDb  = overDb * (1.0f / ratio - 1.0f);
            lastGR = gainDb;
            return in * juce::Decibels::decibelsToGain(gainDb);
        }
        lastGR = 0.0f;
        return in;
    }

    float getGainReductionDb() const { return lastGR; }

private:
    double sampleRate = 48000.0;
    float  envelope = 0.0f;
    float  attCoef = 0.9f, relCoef = 0.999f;
    float  lastGR = 0.0f;
};

// ==============================================================================
// OPTO COMPRESSOR — LA-2A Style
// ==============================================================================
class OPTOCompressor
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        attCoef    = std::exp(-1.0f / (0.010f * (float)sampleRate));
        relCoefSlow = std::exp(-1.0f / (2.0f   * (float)sampleRate));
        relCoefFast = std::exp(-1.0f / (0.060f * (float)sampleRate));
        envelope = 0.0f;
    }

    float process(float in, float peakRedPercent)
    {
        float thresh = peakRedPercent / 100.0f;
        float absIn  = std::abs(in);
        float relC   = (envelope > 0.5f) ? relCoefFast : relCoefSlow;
        envelope = (absIn > envelope)
            ? attCoef * envelope + (1.0f - attCoef) * absIn
            : relC    * envelope + (1.0f - relC)    * absIn;

        if (envelope > thresh && thresh > 0.001f)
        {
            float reduction = thresh / (envelope + 1e-9f);
            lastGR = 1.0f - reduction;
            return in * reduction;
        }
        lastGR = 0.0f;
        return in;
    }

    float getGainReduction() const { return lastGR; }

private:
    double sampleRate = 48000.0;
    float  envelope = 0.0f;
    float  attCoef, relCoefSlow, relCoefFast;
    float  lastGR = 0.0f;
};

// ==============================================================================
// HG-2 SATURATOR — Chaleur tube/harmonique
// ==============================================================================
class HG2Saturator
{
public:
    void prepare(double sr) { sampleRate = sr; }

    float process(float in, float saturation, float pentode = 0.1f, float triode = 0.1f)
    {
        float x = in * (1.0f + saturation * 2.5f);
        // Harmoniques pairs (tube) — asymétrie
        float p = (x >= 0.0f)
            ? 1.0f - std::exp(-x)
            : -1.0f + std::exp(x * 0.75f);
        // Harmoniques impairs (triode)
        float t = std::tanh(x * (1.0f + triode));
        float out = (p * (0.5f + pentode) + t * (0.5f - pentode * 0.5f)) * 0.8f;
        // Normalisation douce pour éviter le clipping
        return out / (1.0f + std::abs(out) * 0.1f);
    }

private:
    double sampleRate = 48000.0;
};

// ==============================================================================
// R-VOX — Vocal Compressor/Gate
// ==============================================================================
class RVoxProcessor
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, 512, 1 };
        comp.prepare(spec);
        comp.setRatio(4.0f);
        comp.setAttack(5.0f);
        comp.setRelease(80.0f);
    }

    float process(float in, float threshDb, float gateThresh = 0.001f)
    {
        if (std::abs(in) < gateThresh) return 0.0f;
        comp.setThreshold(threshDb);
        return comp.processSample(0, in);
    }

private:
    double sampleRate = 48000.0;
    juce::dsp::Compressor<float> comp;
};

// ==============================================================================
// DE-ESSER 902
// ==============================================================================
class DeEsser902
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, 512, 1 };
        detector.prepare(spec);
        *detector.coefficients = *juce::dsp::IIR::Coefficients<float>::makeBandPass(sr, 7500.0f, 2.0f);
    }

    float process(float in, float range)
    {
        if (range < 0.001f) return in;
        float sibilance = detector.processSample(in);
        float level     = std::abs(sibilance);
        float reduction = 1.0f - juce::jlimit(0.0f, 0.9f, range * level * 8.0f);
        return in * reduction;
    }

private:
    double sampleRate = 48000.0;
    juce::dsp::IIR::Filter<float> detector;
};

// ==============================================================================
// STEREO MAKER — Mid/Side Width
// ==============================================================================
class StereoMaker
{
public:
    void process(juce::AudioBuffer<float>& buffer, float width)
    {
        if (buffer.getNumChannels() < 2) return;
        auto* l = buffer.getWritePointer(0);
        auto* r = buffer.getWritePointer(1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float mid  = (l[i] + r[i]) * 0.5f;
            float side = (l[i] - r[i]) * 0.5f * width;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }
};

// ==============================================================================
// PITCH SHIFTER — Phase vocoder simple
// ==============================================================================
class NADAPitchShifter
{
public:
    static constexpr int FFT_ORDER = 11;
    static constexpr int FFT_SIZE  = 1 << FFT_ORDER;   // 2048
    static constexpr int HOP_SIZE  = FFT_SIZE / 4;      // 512

    NADAPitchShifter() : fft(FFT_ORDER)
    {
        inputFifo.resize(FFT_SIZE, 0.0f);
        outputFifo.resize(FFT_SIZE * 4, 0.0f);
        fftBuf.resize(FFT_SIZE * 2, 0.0f);
        lastPhase.resize(FFT_SIZE / 2 + 1, 0.0f);
        phaseAccum.resize(FFT_SIZE / 2 + 1, 0.0f);
        window.resize(FFT_SIZE);
        for (int i = 0; i < FFT_SIZE; ++i)
            window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (FFT_SIZE - 1)));
    }

    void prepare(double sr, int /*blockSize*/) { sampleRate = sr; reset(); }

    void reset()
    {
        std::fill(inputFifo.begin(),  inputFifo.end(),  0.0f);
        std::fill(outputFifo.begin(), outputFifo.end(), 0.0f);
        std::fill(lastPhase.begin(),  lastPhase.end(),  0.0f);
        std::fill(phaseAccum.begin(), phaseAccum.end(), 0.0f);
        inputPos  = 0;
        outputPos = 0;
        hopCount  = 0;
    }

    void process(juce::AudioBuffer<float>& buffer, float pitchRatio)
    {
        // Si ratio neutre, bypass total pour éviter latence
        if (std::abs(pitchRatio - 1.0f) < 0.005f) return;

        const int numSamples = buffer.getNumSamples();
        const int numCh      = buffer.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            // Moyenne des canaux en entrée
            float inSample = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                inSample += buffer.getSample(ch, i);
            inSample /= (float)numCh;

            inputFifo[inputPos] = inSample;
            inputPos = (inputPos + 1) % FFT_SIZE;
            ++hopCount;

            if (hopCount >= HOP_SIZE)
            {
                hopCount = 0;
                shiftPitch(pitchRatio);
            }

            float outSample = outputFifo[outputPos];
            outputFifo[outputPos] = 0.0f;
            outputPos = (outputPos + 1) % (int)outputFifo.size();

            for (int ch = 0; ch < numCh; ++ch)
                buffer.setSample(ch, i, outSample);
        }
    }

private:
    void shiftPitch(float ratio)
    {
        // Copie + fenêtrage
        for (int i = 0; i < FFT_SIZE; ++i)
        {
            int idx   = (inputPos + i) % FFT_SIZE;
            fftBuf[i] = inputFifo[idx] * window[i];
        }
        std::fill(fftBuf.begin() + FFT_SIZE, fftBuf.end(), 0.0f);

        fft.performRealOnlyForwardTransform(fftBuf.data(), true);

        const float twoPi    = juce::MathConstants<float>::twoPi;
        const float phaseInc = twoPi * HOP_SIZE / FFT_SIZE;

        for (int k = 0; k <= FFT_SIZE / 2; ++k)
        {
            float re = fftBuf[2 * k];
            float im = fftBuf[2 * k + 1];
            float mag   = std::sqrt(re * re + im * im);
            float phase = std::atan2(im, re);

            float dPhase = phase - lastPhase[k] - k * phaseInc;
            // Repliement de phase
            dPhase -= twoPi * std::round(dPhase / twoPi);
            float trueFreq = (float)k * phaseInc + dPhase;
            phaseAccum[k] += ratio * trueFreq;
            lastPhase[k]   = phase;

            fftBuf[2 * k]     = mag * std::cos(phaseAccum[k]);
            fftBuf[2 * k + 1] = mag * std::sin(phaseAccum[k]);
        }

        fft.performRealOnlyInverseTransform(fftBuf.data());

        // Overlap-add
        float scale = 2.0f / (float)FFT_SIZE;
        for (int i = 0; i < FFT_SIZE; ++i)
        {
            int outIdx = (outputPos + i) % (int)outputFifo.size();
            outputFifo[outIdx] += fftBuf[i] * window[i] * scale;
        }
    }

    double sampleRate = 48000.0;
    juce::dsp::FFT fft;
    std::vector<float> inputFifo, outputFifo;
    std::vector<float> fftBuf;
    std::vector<float> lastPhase, phaseAccum;
    std::vector<float> window;
    int inputPos = 0, outputPos = 0, hopCount = 0;
};

// ==============================================================================
// NADA AUDIO PROCESSOR — Le cerveau
// ==============================================================================
class NADAAudioProcessor : public juce::AudioProcessor,
                           public juce::Timer
{
public:
    // ── AI State exposé à l'UI ──────────────────────────────────────────────
    struct AIState
    {
        bool         aiEnabled   = false;
        bool         isAnalyzing = false;
        juce::String statusText  = "NADA BOSS // READY";
        AISpectralAnalyzer::VocalProfile lastProfile;
        AIMixer::MixingParameters        lastMixParams;
    };

    NADAAudioProcessor();
    ~NADAAudioProcessor() override;

    // ── JUCE overrides ──────────────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock  (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void timerCallback() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName()          const override { return "NADA BOSS Vocal Suite"; }
    bool  acceptsMidi()                   const override { return false; }
    bool  producesMidi()                  const override { return false; }
    double getTailLengthSeconds()         const override { return 2.0; }
    int   getNumPrograms()                      override { return 1; }
    int   getCurrentProgram()                   override { return 0; }
    void  setCurrentProgram(int)               override {}
    const juce::String getProgramName(int)      override { return "Default"; }
    void  changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ── Public API ──────────────────────────────────────────────────────────
    void    triggerNADAAnalysis();
    AIState getAIState() const { return aiState; }

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateDSPFromParams();
    void runAIAnalysis();
    void applyAIParams(const AIMixer::MixingParameters& p);

    // ── DSP Chain ───────────────────────────────────────────────────────────
    NADAPitchShifter pitchShifter;

    struct ProQ3EQ
    {
        juce::dsp::IIR::Filter<float> bands[6];
    } eq6;

    struct PultecEQ
    {
        juce::dsp::IIR::Filter<float> low, high;
    } pultec;

    struct SSLChannel
    {
        juce::dsp::IIR::Filter<float> bands[4];
    } ssl;

    FETCompressor  fet1176;
    OPTOCompressor optoLA2A;
    HG2Saturator   hg2;
    RVoxProcessor  rvox;
    DeEsser902     deesser;
    StereoMaker    stereoMaker;

    juce::dsp::Reverb         reverb;
    juce::dsp::Limiter<float> limiter;

    // Delay — buffers manuels pour un mix parallèle propre
    struct StereoDelay
    {
        std::vector<float> bufL, bufR;
        int writePos     = 0;
        int delaySamples = 0;
        float feedback   = 0.0f;
        double sampleRate = 48000.0;

        void prepare(double sr, int maxDelaySamples)
        {
            sampleRate = sr;
            bufL.assign(maxDelaySamples, 0.0f);
            bufR.assign(maxDelaySamples, 0.0f);
            writePos = 0;
        }

        void setDelayMs(float ms)
        {
            delaySamples = juce::jlimit(1, (int)bufL.size() - 1,
                                        (int)(sampleRate * ms / 1000.0));
        }

        // Retourne le sample delayed sans modifier le dry
        float readL() const
        {
            int idx = ((int)bufL.size() + writePos - delaySamples) % (int)bufL.size();
            return bufL[idx];
        }
        float readR() const
        {
            int idx = ((int)bufR.size() + writePos - delaySamples) % (int)bufR.size();
            return bufR[idx];
        }

        void writeAndAdvance(float l, float r)
        {
            bufL[writePos] = l + readL() * feedback;
            bufR[writePos] = r + readR() * feedback;
            writePos = (writePos + 1) % (int)bufL.size();
        }
    } stereoDelay;

    // ── AI ──────────────────────────────────────────────────────────────────
    AISpectralAnalyzer          spectralAnalyzer;
    AIMixer                     aiMixer;
    std::vector<float>          analysisBuffer;
    int                         analysisBufferPos = 0;
    std::atomic<bool>           analysisRequested { false };
    AIState                     aiState;

    // ── Smoothed params (évite les clicks) ──────────────────────────────────
    juce::SmoothedValue<float> smoothedFetThresh;
    juce::SmoothedValue<float> smoothedFetRatio;
    juce::SmoothedValue<float> smoothedOptoRed;
    juce::SmoothedValue<float> smoothedSatDrive;
    juce::SmoothedValue<float> smoothedRvoxComp;
    juce::SmoothedValue<float> smoothedDeesserRange;
    juce::SmoothedValue<float> smoothedWidth;
    juce::SmoothedValue<float> smoothedReverbMix;
    juce::SmoothedValue<float> smoothedDelayMix;

    double mSampleRate    = 48000.0;
    int    mSamplesPerBlock = 512;

    // EQ change detection
    float prevEqValues[6][3] = {};
    float prevPultecLow = -999.f, prevPultecHigh = -999.f;
    float prevSslDrive  = -999.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NADAAudioProcessor)
};
