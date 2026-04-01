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
    struct PreviewMatchPoint
    {
        float frequencyHz = 0.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
    };

    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int fftHopSize = fftSize / 2;
    static constexpr int spectrumBinCount = fftSize / 2;
    static constexpr int previewBandCount = 5;
    static constexpr float spectrumSmoothingAlpha = 0.2f;
    static constexpr int previewDifferenceSmoothingRadius = 4;
    static constexpr float previewEqSmoothingTimeSeconds = 0.08f;
    static constexpr float previewEqMixSmoothingTimeSeconds = 0.03f;
    static constexpr auto previewEqEnabledParamID = "previewEqEnabled";
    static constexpr auto previewEqBypassedParamID = "previewEqBypassed";
    static constexpr auto previewBlendAmountParamID = "previewBlendAmount";
    static constexpr auto previewOutputGainParamID = "previewOutputGainDb";

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

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
    std::array<float, spectrumBinCount> getPreviewDifferenceSpectrumDb() const noexcept;
    std::array<float, previewBandCount> getPreviewBandAdjustmentsDb() const noexcept;
    std::array<PreviewMatchPoint, previewBandCount> getPreviewMatchPoints() const noexcept;
    std::array<float, previewBandCount> getCurrentPreviewEqBandGainsDb() const noexcept;
    float getPreviewOutputTrimDb() const noexcept;
    void setPreviewEqBypassed (bool shouldBeBypassed) noexcept;
    bool isPreviewEqBypassed() const noexcept;
    void setPreviewEqEnabled (bool shouldBeEnabled) noexcept;
    bool isPreviewEqEnabled() const noexcept;
    bool isPreviewEqActive() const noexcept;
    void setPreviewOutputGainDb (float newGainDb) noexcept;
    float getPreviewOutputGainDb() const noexcept;
    void setPreviewBlendAmount (float newAmount) noexcept;
    float getPreviewBlendAmount() const noexcept;
    bool loadReferenceFile (const juce::File& file);
    void clearReferenceTrack();
    bool hasReferenceTrack() const noexcept;
    juce::String getReferenceTrackName() const;
    juce::String getReferenceTrackInfo() const;
    bool hasReferenceLoadError() const noexcept;
    juce::AudioProcessorValueTreeState parameters;

private:
    using PreviewFilter = juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>,
                                                         juce::dsp::IIR::Coefficients<float>>;

    void pushNextSampleIntoFifo (float sample) noexcept;
    void performFrequencyAnalysis() noexcept;
    void updatePreviewFilterCoefficients (int numSamples) noexcept;
    void applyPreviewEq (juce::AudioBuffer<float>& buffer) noexcept;

    juce::dsp::FFT forwardFFT { fftOrder };
    juce::dsp::WindowingFunction<float> windowingFunction {
        static_cast<size_t> (fftSize),
        juce::dsp::WindowingFunction<float>::hann
    };

    std::array<float, fftSize> analysisFifo {};
    std::array<float, fftSize * 2> fftData {};
    std::array<std::array<float, spectrumBinCount>, 2> spectrumBuffers {};
    std::array<std::array<float, spectrumBinCount>, 2> referenceSpectrumBuffers {};
    std::array<PreviewFilter, previewBandCount> previewFilters {};
    std::array<juce::LinearSmoothedValue<float>, previewBandCount> previewBandGainSmoothers {};
    std::array<std::atomic<float>, previewBandCount> currentPreviewBandGainsDb {};
    juce::LinearSmoothedValue<float> previewWetMixSmoother;
    juce::AudioBuffer<float> previewEqBuffer;
    std::atomic<int> activeSpectrumBufferIndex { 0 };
    std::atomic<int> activeReferenceSpectrumBufferIndex { 0 };
    std::atomic<bool> referenceTrackLoaded { false };
    juce::AudioFormatManager audioFormatManager;
    juce::String referenceTrackName;
    juce::String referenceTrackInfo;
    juce::String referenceTrackPath;
    std::atomic<bool> referenceLoadError { false };
    double currentSampleRate = 44100.0;
    int fifoIndex = 0;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BolbolRefMasterAudioProcessor)
};
