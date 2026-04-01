/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <array>
#include <atomic>

#include <JuceHeader.h>

//==============================================================================
/**
*/
class BolbolRefMasterAudioProcessor  : public juce::AudioProcessor
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int fftHopSize = fftSize / 2;
    static constexpr int spectrumBinCount = fftSize / 2;
    static constexpr float spectrumSmoothingAlpha = 0.2f;

    //==============================================================================
    BolbolRefMasterAudioProcessor();
    ~BolbolRefMasterAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    std::array<float, spectrumBinCount> getLatestMagnitudeSpectrum() const noexcept;
    std::array<float, spectrumBinCount> getReferenceMagnitudeSpectrum() const noexcept;
    bool loadReferenceFile (const juce::File& file);
    bool hasReferenceTrack() const noexcept;
    juce::String getReferenceTrackName() const;
    juce::String getReferenceTrackInfo() const;

private:
    void pushNextSampleIntoFifo (float sample) noexcept;
    void performFrequencyAnalysis() noexcept;

    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> windowingFunction {
        static_cast<size_t> (fftSize),
        juce::dsp::WindowingFunction<float>::hann
    };

    std::array<float, fftSize> analysisFifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<std::array<float, spectrumBinCount>, 2> spectrumBuffers {};
    std::array<std::array<float, spectrumBinCount>, 2> referenceSpectrumBuffers {};
    std::atomic<int> activeSpectrumBufferIndex { 0 };
    std::atomic<int> activeReferenceSpectrumBufferIndex { 0 };
    std::atomic<bool> referenceTrackLoaded { false };
    juce::AudioFormatManager audioFormatManager;
    juce::String referenceTrackName;
    juce::String referenceTrackInfo;
    int fifoIndex = 0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BolbolRefMasterAudioProcessor)
};
