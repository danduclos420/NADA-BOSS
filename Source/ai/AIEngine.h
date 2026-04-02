#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>
#include <cmath>

// ==============================================================================
// AI SPECTRAL ANALYZER
// Analyse les caractéristiques vocales via FFT
// Tourne dans un thread séparé — JAMAIS dans l'audio thread
// ==============================================================================
class AISpectralAnalyzer
{
public:
    struct VocalProfile
    {
        float lowFreqEnergy    = 0.0f;   // 50-200 Hz   (rumble/mud)
        float lowMidEnergy     = 0.0f;   // 200-500 Hz  (boxiness)
        float midEnergy        = 0.0f;   // 500-2k Hz   (body)
        float presenceEnergy   = 0.0f;   // 2k-5k Hz    (clarity)
        float sibilanceEnergy  = 0.0f;   // 5k-10k Hz   (sibilance)
        float brillianceEnergy = 0.0f;   // 10k-20k Hz  (air)
        float dynamicRange     = 0.0f;   // crest factor dB
        float rmsLevel         = 0.0f;   // niveau moyen
    };

    AISpectralAnalyzer() : fft(11)       // order 11 = 2048 points
    {
        fftSize = 2048;
        window.resize(fftSize);
        fftData.resize(fftSize * 2, 0.0f);
        magnitudes.resize(fftSize / 2, 0.0f);

        for (int i = 0; i < fftSize; ++i)
            window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1)));
    }

    VocalProfile analyzeBuffer(const std::vector<float>& audioBuffer, float sampleRate = 48000.0f)
    {
        VocalProfile profile;
        if ((int)audioBuffer.size() < fftSize) return profile;

        // Fenêtrage + copie
        for (int i = 0; i < fftSize; ++i)
        {
            fftData[i]           = audioBuffer[i] * window[i];
            fftData[i + fftSize] = 0.0f;
        }

        fft.performFrequencyOnlyForwardTransform(fftData.data());

        // Magnitudes
        for (int i = 0; i < fftSize / 2; ++i)
            magnitudes[i] = fftData[i];

        // Énergie par bande
        profile.lowFreqEnergy    = getBandEnergy(50.f,    200.f,   sampleRate);
        profile.lowMidEnergy     = getBandEnergy(200.f,   500.f,   sampleRate);
        profile.midEnergy        = getBandEnergy(500.f,   2000.f,  sampleRate);
        profile.presenceEnergy   = getBandEnergy(2000.f,  5000.f,  sampleRate);
        profile.sibilanceEnergy  = getBandEnergy(5000.f,  10000.f, sampleRate);
        profile.brillianceEnergy = getBandEnergy(10000.f, 20000.f, sampleRate);

        // RMS + dynamic range
        float maxSample = 0.0f, sumSq = 0.0f;
        for (float s : audioBuffer)
        {
            float a = std::abs(s);
            if (a > maxSample) maxSample = a;
            sumSq += s * s;
        }
        float rms = std::sqrt(sumSq / (float)audioBuffer.size());
        profile.rmsLevel    = rms;
        profile.dynamicRange = (rms > 1e-6f)
            ? 20.0f * std::log10(maxSample / rms)
            : 0.0f;

        return profile;
    }

private:
    float getBandEnergy(float freqLow, float freqHigh, float sampleRate)
    {
        int binLow  = juce::jlimit(0, fftSize / 2 - 1, (int)(freqLow  * fftSize / sampleRate));
        int binHigh = juce::jlimit(0, fftSize / 2 - 1, (int)(freqHigh * fftSize / sampleRate));
        float sum = 0.0f;
        for (int i = binLow; i <= binHigh; ++i)
            sum += magnitudes[i];
        return sum / std::max(1, binHigh - binLow + 1);
    }

    int fftSize;
    juce::dsp::FFT fft;
    std::vector<float> window;
    std::vector<float> fftData;
    std::vector<float> magnitudes;
};

// ==============================================================================
// AI MIXER
// Génère des paramètres de mixage basés sur l'analyse spectrale
// Logique "règles intelligentes" — pas de ML requis pour v1
// ==============================================================================
class AIMixer
{
public:
    struct EQBand
    {
        float frequency = 1000.0f;
        float gain      = 0.0f;
        float q         = 1.0f;
    };

    struct MixingParameters
    {
        std::array<EQBand, 6> eqBands;
        float fet1176Threshold  = -20.0f;
        float fet1176Ratio      = 4.0f;
        float optoLA2AReduction = 30.0f;
        float hg2Saturation     = 0.1f;
        float deesserRange      = 0.2f;
        float limiterThreshold  = -1.0f;
        float reverbMix         = 0.12f;
        float delayMix          = 0.08f;
    };

    MixingParameters generateMixingParameters(const AISpectralAnalyzer::VocalProfile& p)
    {
        MixingParameters out;

        // Band 1 — High-pass / coupe rumble
        out.eqBands[0].frequency = 80.0f;
        out.eqBands[0].gain      = (p.lowFreqEnergy > 0.3f) ? -4.0f : 0.0f;
        out.eqBands[0].q         = 0.7f;

        // Band 2 — Coupe boxiness 200-400 Hz
        out.eqBands[1].frequency = 280.0f;
        out.eqBands[1].gain      = (p.lowMidEnergy > 0.5f) ? -3.0f : -1.0f;
        out.eqBands[1].q         = 1.2f;

        // Band 3 — Corps vocal 800 Hz
        out.eqBands[2].frequency = 800.0f;
        out.eqBands[2].gain      = (p.midEnergy < 0.3f) ? 2.0f : 0.0f;
        out.eqBands[2].q         = 1.0f;

        // Band 4 — Présence / clarté 3kHz
        out.eqBands[3].frequency = 3000.0f;
        out.eqBands[3].gain      = (p.presenceEnergy < 0.4f) ? 3.0f : 1.5f;
        out.eqBands[3].q         = 2.0f;

        // Band 5 — Sibilance (coupe si trop)
        out.eqBands[4].frequency = 7000.0f;
        out.eqBands[4].gain      = (p.sibilanceEnergy > 0.6f) ? -2.5f : 0.0f;
        out.eqBands[4].q         = 1.5f;

        // Band 6 — Air 12 kHz
        out.eqBands[5].frequency = 12000.0f;
        out.eqBands[5].gain      = (p.brillianceEnergy < 0.3f) ? 3.5f : 1.0f;
        out.eqBands[5].q         = 0.8f;

        // Compression FET 1176
        out.fet1176Threshold = -20.0f - (p.dynamicRange > 12.0f ? 4.0f : 0.0f);
        out.fet1176Ratio     = (p.dynamicRange > 15.0f) ? 8.0f : 4.0f;

        // LA-2A
        out.optoLA2AReduction = 25.0f + (p.dynamicRange > 10.0f ? 10.0f : 0.0f);

        // Saturation HG2
        out.hg2Saturation = (p.presenceEnergy > 0.6f && p.brillianceEnergy < 0.4f) ? 0.2f : 0.08f;

        // De-esser
        out.deesserRange = (p.sibilanceEnergy > 0.6f) ? 0.5f : 0.2f;

        // Reverb/Delay — adapté au niveau du signal
        out.reverbMix = (p.rmsLevel < 0.05f) ? 0.15f : 0.10f;
        out.delayMix  = 0.08f;

        return out;
    }
};
