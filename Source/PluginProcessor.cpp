/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace
{
juce::String formatReferenceDuration (double seconds)
{
    const auto totalSeconds = juce::jmax (0, juce::roundToInt (seconds));
    const auto minutes = totalSeconds / 60;
    const auto remainingSeconds = totalSeconds % 60;
    return juce::String (minutes) + ":" + juce::String (remainingSeconds).paddedLeft ('0', 2);
}

float calculateSpectrumAverageDb (
    const std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount>& spectrum,
    double sampleRate)
{
    const auto minFrequency = 20.0f;
    const auto maxFrequency = juce::jmin (20000.0f, static_cast<float> (sampleRate * 0.5));
    const auto binWidth = static_cast<float> (sampleRate / BolbolRefMasterAudioProcessor::fftSize);

    float sumDb = 0.0f;
    int count = 0;

    for (int bin = 1; bin < BolbolRefMasterAudioProcessor::spectrumBinCount; ++bin)
    {
        const auto frequency = static_cast<float> (bin) * binWidth;

        if (frequency < minFrequency || frequency > maxFrequency)
            continue;

        sumDb += juce::Decibels::gainToDecibels (juce::jmax (spectrum[static_cast<size_t> (bin)], 1.0e-5f));
        ++count;
    }

    return count > 0 ? (sumDb / static_cast<float> (count)) : 0.0f;
}

void assignIirCoefficients (juce::dsp::IIR::Coefficients<float>& destination,
                            const std::array<float, 6>& source) noexcept
{
    auto* raw = destination.getRawCoefficients();

    for (size_t i = 0; i < source.size(); ++i)
        raw[i] = source[i];
}
}

//==============================================================================
BolbolRefMasterAudioProcessor::BolbolRefMasterAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    audioFormatManager.registerBasicFormats();
}

BolbolRefMasterAudioProcessor::~BolbolRefMasterAudioProcessor()
{
}

//==============================================================================
const juce::String BolbolRefMasterAudioProcessor::getName() const
{
    return "Bolbol RefMaster";
}

bool BolbolRefMasterAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BolbolRefMasterAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BolbolRefMasterAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BolbolRefMasterAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BolbolRefMasterAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int BolbolRefMasterAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BolbolRefMasterAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String BolbolRefMasterAudioProcessor::getProgramName (int index)
{
    return {};
}

void BolbolRefMasterAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void BolbolRefMasterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);

    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    currentSampleRate = sampleRate;
    fifoIndex = 0;
    activeSpectrumBufferIndex.store (0, std::memory_order_release);
    activeReferenceSpectrumBufferIndex.store (0, std::memory_order_release);

    analysisFifo.fill (0.0f);
    fftData.fill (0.0f);

    for (auto& spectrumBuffer : spectrumBuffers)
        spectrumBuffer.fill (0.0f);

    for (auto& referenceSpectrumBuffer : referenceSpectrumBuffers)
        referenceSpectrumBuffer.fill (0.0f);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (juce::jmax (1, getTotalNumOutputChannels()));

    for (auto& previewFilter : previewFilters)
    {
        previewFilter.prepare (spec);
        previewFilter.reset();
    }

    for (auto& smoother : previewBandGainSmoothers)
    {
        smoother.reset (sampleRate, previewEqSmoothingTimeSeconds);
        smoother.setCurrentAndTargetValue (0.0f);
    }

    previewWetMixSmoother.reset (sampleRate, previewEqMixSmoothingTimeSeconds);
    previewWetMixSmoother.setCurrentAndTargetValue (0.0f);
    previewEqBuffer.setSize (static_cast<int> (spec.numChannels), samplesPerBlock, false, false, true);
}

void BolbolRefMasterAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BolbolRefMasterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void BolbolRefMasterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    if (totalNumInputChannels <= 0)
        return;

    const auto* const* channelData = buffer.getArrayOfReadPointers();
    const auto inverseInputChannelCount = 1.0f / static_cast<float> (totalNumInputChannels);

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        float monoSample = 0.0f;

        for (int channel = 0; channel < totalNumInputChannels; ++channel)
            monoSample += channelData[channel][sample];

        pushNextSampleIntoFifo (monoSample * inverseInputChannelCount);
    }

    updatePreviewFilterCoefficients (buffer.getNumSamples());
    applyPreviewEq (buffer);
}

//==============================================================================
bool BolbolRefMasterAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* BolbolRefMasterAudioProcessor::createEditor()
{
    return new BolbolRefMasterAudioProcessorEditor (*this);
}

//==============================================================================
void BolbolRefMasterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("BolbolRefMasterState");
    state.setProperty ("previewEqEnabled", isPreviewEqEnabled(), nullptr);
    state.setProperty ("previewEqBypassed", isPreviewEqBypassed(), nullptr);
    state.setProperty ("previewBlendAmount", getPreviewBlendAmount(), nullptr);
    state.setProperty ("referenceTrackPath", referenceTrackLoaded.load (std::memory_order_acquire) ? referenceTrackPath : juce::String(), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BolbolRefMasterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes < 1)
        return;

    if (const auto xmlState = getXmlFromBinary (data, sizeInBytes))
    {
        juce::ValueTree state = juce::ValueTree::fromXml (*xmlState);

        setPreviewEqEnabled (static_cast<bool> (state.getProperty ("previewEqEnabled", false)));
        setPreviewEqBypassed (static_cast<bool> (state.getProperty ("previewEqBypassed", false)));
        setPreviewBlendAmount (static_cast<float> (static_cast<double> (state.getProperty ("previewBlendAmount", 0.5))));

        const auto referencePath = state.getProperty ("referenceTrackPath").toString();

        if (referencePath.isNotEmpty())
            loadReferenceFile (juce::File (referencePath));
    }
    else
    {
        juce::MemoryInputStream stream (data, static_cast<size_t> (sizeInBytes), false);

        setPreviewBlendAmount (stream.readFloat());
    }
}

std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount> BolbolRefMasterAudioProcessor::getLatestMagnitudeSpectrum() const noexcept
{
    return spectrumBuffers[activeSpectrumBufferIndex.load (std::memory_order_acquire)];
}

std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount> BolbolRefMasterAudioProcessor::getReferenceMagnitudeSpectrum() const noexcept
{
    return referenceSpectrumBuffers[activeReferenceSpectrumBufferIndex.load (std::memory_order_acquire)];
}

std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount> BolbolRefMasterAudioProcessor::getPreviewDifferenceSpectrumDb() const noexcept
{
    std::array<float, spectrumBinCount> differenceSpectrum {};
    std::array<float, spectrumBinCount> smoothedDifferenceSpectrum {};

    if (! hasReferenceTrack())
        return differenceSpectrum;

    const auto sampleRate = juce::jmax (getSampleRate(), 44100.0);
    const auto inputSpectrum = getLatestMagnitudeSpectrum();
    const auto referenceSpectrum = getReferenceMagnitudeSpectrum();
    const auto inputAverageDb = calculateSpectrumAverageDb (inputSpectrum, sampleRate);
    const auto referenceAverageDb = calculateSpectrumAverageDb (referenceSpectrum, sampleRate);

    for (int bin = 0; bin < spectrumBinCount; ++bin)
    {
        const auto index = static_cast<size_t> (bin);
        const auto inputDb = juce::Decibels::gainToDecibels (juce::jmax (inputSpectrum[index], 1.0e-5f)) - inputAverageDb;
        const auto referenceDb = juce::Decibels::gainToDecibels (juce::jmax (referenceSpectrum[index], 1.0e-5f)) - referenceAverageDb;
        differenceSpectrum[index] = (referenceDb - inputDb) * getPreviewBlendAmount();
    }

    for (int bin = 0; bin < spectrumBinCount; ++bin)
    {
        float sum = 0.0f;
        int count = 0;

        for (int offset = -previewDifferenceSmoothingRadius; offset <= previewDifferenceSmoothingRadius; ++offset)
        {
            const auto smoothedIndex = juce::jlimit (0, spectrumBinCount - 1, bin + offset);
            sum += differenceSpectrum[static_cast<size_t> (smoothedIndex)];
            ++count;
        }

        smoothedDifferenceSpectrum[static_cast<size_t> (bin)] = sum / static_cast<float> (count);
    }

    return smoothedDifferenceSpectrum;
}

std::array<float, BolbolRefMasterAudioProcessor::previewBandCount> BolbolRefMasterAudioProcessor::getPreviewBandAdjustmentsDb() const noexcept
{
    std::array<float, previewBandCount> bandAdjustments {};

    if (! hasReferenceTrack())
        return bandAdjustments;

    constexpr std::array<std::pair<float, float>, previewBandCount> ranges {{
        { 20.0f, 80.0f },
        { 80.0f, 250.0f },
        { 250.0f, 2000.0f },
        { 2000.0f, 8000.0f },
        { 8000.0f, 20000.0f },
    }};

    const auto sampleRate = juce::jmax (getSampleRate(), 44100.0);
    const auto binWidth = static_cast<float> (sampleRate / fftSize);
    const auto differenceSpectrum = getPreviewDifferenceSpectrumDb();

    for (size_t bandIndex = 0; bandIndex < ranges.size(); ++bandIndex)
    {
        const auto [lowHz, highHz] = ranges[bandIndex];
        float sum = 0.0f;
        int count = 0;

        for (int bin = 1; bin < spectrumBinCount; ++bin)
        {
            const auto frequency = static_cast<float> (bin) * binWidth;

            if (frequency < lowHz || frequency >= highHz)
                continue;

            sum += differenceSpectrum[static_cast<size_t> (bin)];
            ++count;
        }

        if (count > 0)
            bandAdjustments[bandIndex] = juce::jlimit (-6.0f, 6.0f, sum / static_cast<float> (count));
    }

    return bandAdjustments;
}

std::array<BolbolRefMasterAudioProcessor::PreviewMatchPoint, BolbolRefMasterAudioProcessor::previewBandCount>
BolbolRefMasterAudioProcessor::getPreviewMatchPoints() const noexcept
{
    std::array<PreviewMatchPoint, previewBandCount> matchPoints {};
    const auto bandAdjustments = getPreviewBandAdjustmentsDb();

    constexpr std::array<std::pair<float, float>, previewBandCount> ranges {{
        { 20.0f, 80.0f },
        { 80.0f, 250.0f },
        { 250.0f, 2000.0f },
        { 2000.0f, 8000.0f },
        { 8000.0f, 20000.0f },
    }};

    for (size_t bandIndex = 0; bandIndex < ranges.size(); ++bandIndex)
    {
        const auto [lowHz, highHz] = ranges[bandIndex];
        const auto centreFrequency = std::sqrt (lowHz * highHz);
        const auto octaveSpan = std::log2 (highHz / lowHz);

        matchPoints[bandIndex] = PreviewMatchPoint {
            centreFrequency,
            bandAdjustments[bandIndex],
            juce::jmax (0.4f, 1.0f / octaveSpan)
        };
    }

    return matchPoints;
}

std::array<float, BolbolRefMasterAudioProcessor::previewBandCount> BolbolRefMasterAudioProcessor::getCurrentPreviewEqBandGainsDb() const noexcept
{
    std::array<float, previewBandCount> gains {};

    for (size_t i = 0; i < gains.size(); ++i)
        gains[i] = currentPreviewBandGainsDb[i].load (std::memory_order_acquire);

    return gains;
}

float BolbolRefMasterAudioProcessor::getPreviewOutputTrimDb() const noexcept
{
    const auto bandAdjustments = getPreviewBandAdjustmentsDb();
    float sum = 0.0f;

    for (auto gainDb : bandAdjustments)
        sum += gainDb;

    const auto averageGainDb = sum / static_cast<float> (bandAdjustments.size());
    return juce::jlimit (-3.0f, 3.0f, -averageGainDb * 0.35f);
}

void BolbolRefMasterAudioProcessor::setPreviewBlendAmount (float newAmount) noexcept
{
    previewBlendAmount.store (juce::jlimit (0.0f, 1.0f, newAmount), std::memory_order_release);
}

float BolbolRefMasterAudioProcessor::getPreviewBlendAmount() const noexcept
{
    return previewBlendAmount.load (std::memory_order_acquire);
}

void BolbolRefMasterAudioProcessor::setPreviewEqEnabled (bool shouldBeEnabled) noexcept
{
    previewEqEnabled.store (shouldBeEnabled, std::memory_order_release);
}

bool BolbolRefMasterAudioProcessor::isPreviewEqEnabled() const noexcept
{
    return previewEqEnabled.load (std::memory_order_acquire);
}

bool BolbolRefMasterAudioProcessor::isPreviewEqActive() const noexcept
{
    return hasReferenceTrack()
        && isPreviewEqEnabled()
        && ! isPreviewEqBypassed()
        && getPreviewBlendAmount() > 0.001f;
}

void BolbolRefMasterAudioProcessor::setPreviewEqBypassed (bool shouldBeBypassed) noexcept
{
    previewEqBypassed.store (shouldBeBypassed, std::memory_order_release);
}

bool BolbolRefMasterAudioProcessor::isPreviewEqBypassed() const noexcept
{
    return previewEqBypassed.load (std::memory_order_acquire);
}

bool BolbolRefMasterAudioProcessor::loadReferenceFile (const juce::File& file)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader> (audioFormatManager.createReaderFor (file));

    if (reader == nullptr)
        return false;

    juce::dsp::FFT referenceFFT { fftOrder };
    juce::dsp::WindowingFunction<float> referenceWindow {
        static_cast<size_t> (fftSize),
        juce::dsp::WindowingFunction<float>::hann
    };

    std::array<float, fftSize> localFifo {};
    std::array<float, fftSize * 2> localFftData {};
    std::array<float, spectrumBinCount> averagedSpectrum {};
    int localFifoIndex = 0;
    int frameCount = 0;

    const auto channelCount = juce::jmax (1, static_cast<int> (reader->numChannels));
    juce::AudioBuffer<float> readBuffer (channelCount, fftHopSize);

    auto analyseFrame = [&]()
    {
        std::fill (localFftData.begin(), localFftData.end(), 0.0f);
        std::copy (localFifo.begin(), localFifo.end(), localFftData.begin());

        referenceWindow.multiplyWithWindowingTable (localFftData.data(), static_cast<size_t> (fftSize));
        referenceFFT.performFrequencyOnlyForwardTransform (localFftData.data());

        const auto normalisation = 1.0f / static_cast<float> (fftSize);

        for (int bin = 0; bin < spectrumBinCount; ++bin)
            averagedSpectrum[static_cast<size_t> (bin)] += localFftData[static_cast<size_t> (bin)] * normalisation;

        ++frameCount;
    };

    for (juce::int64 startSample = 0; startSample < reader->lengthInSamples; startSample += fftHopSize)
    {
        const auto samplesToRead = static_cast<int> (juce::jmin<juce::int64> (fftHopSize, reader->lengthInSamples - startSample));
        readBuffer.clear();
        reader->read (&readBuffer, 0, samplesToRead, startSample, true, true);

        for (int sample = 0; sample < samplesToRead; ++sample)
        {
            float monoSample = 0.0f;

            for (int channel = 0; channel < channelCount; ++channel)
                monoSample += readBuffer.getSample (channel, sample);

            localFifo[static_cast<size_t> (localFifoIndex++)] = monoSample / static_cast<float> (channelCount);

            if (localFifoIndex < fftSize)
                continue;

            analyseFrame();

            std::memmove (localFifo.data(),
                          localFifo.data() + fftHopSize,
                          static_cast<size_t> (fftSize - fftHopSize) * sizeof (float));

            localFifoIndex = fftSize - fftHopSize;
        }
    }

    if (frameCount == 0)
        return false;

    const auto inverseFrameCount = 1.0f / static_cast<float> (frameCount);

    for (auto& magnitude : averagedSpectrum)
        magnitude *= inverseFrameCount;

    const auto writeBufferIndex = 1 - activeReferenceSpectrumBufferIndex.load (std::memory_order_relaxed);
    referenceSpectrumBuffers[static_cast<size_t> (writeBufferIndex)] = averagedSpectrum;
    activeReferenceSpectrumBufferIndex.store (writeBufferIndex, std::memory_order_release);

    const auto durationInSeconds = static_cast<double> (reader->lengthInSamples) / reader->sampleRate;
    referenceTrackName = file.getFileName();
    referenceTrackPath = file.getFullPathName();
    referenceTrackInfo = formatReferenceDuration (durationInSeconds)
                       + " · "
                       + juce::String (reader->sampleRate / 1000.0, 1)
                       + "kHz · "
                       + juce::String (reader->bitsPerSample)
                       + "bit";
    referenceTrackLoaded.store (true, std::memory_order_release);
    return true;
}

void BolbolRefMasterAudioProcessor::clearReferenceTrack()
{
    for (auto& referenceSpectrumBuffer : referenceSpectrumBuffers)
        referenceSpectrumBuffer.fill (0.0f);

    activeReferenceSpectrumBufferIndex.store (0, std::memory_order_release);
    referenceTrackName.clear();
    referenceTrackInfo.clear();
    referenceTrackPath.clear();
    referenceTrackLoaded.store (false, std::memory_order_release);
    previewEqEnabled.store (false, std::memory_order_release);
    previewEqBypassed.store (false, std::memory_order_release);

    for (auto& smoother : previewBandGainSmoothers)
    {
        smoother.setCurrentAndTargetValue (0.0f);
        smoother.setTargetValue (0.0f);
    }

    for (auto& currentGain : currentPreviewBandGainsDb)
        currentGain.store (0.0f, std::memory_order_release);
}

bool BolbolRefMasterAudioProcessor::hasReferenceTrack() const noexcept
{
    return referenceTrackLoaded.load (std::memory_order_acquire);
}

juce::String BolbolRefMasterAudioProcessor::getReferenceTrackName() const
{
    return referenceTrackName;
}

juce::String BolbolRefMasterAudioProcessor::getReferenceTrackInfo() const
{
    return referenceTrackInfo;
}

void BolbolRefMasterAudioProcessor::pushNextSampleIntoFifo (float sample) noexcept
{
    analysisFifo[static_cast<size_t> (fifoIndex++)] = sample;

    if (fifoIndex < fftSize)
        return;

    performFrequencyAnalysis();

    std::memmove (analysisFifo.data(),
                  analysisFifo.data() + fftHopSize,
                  static_cast<size_t> (fftSize - fftHopSize) * sizeof (float));

    fifoIndex = fftSize - fftHopSize;
}

void BolbolRefMasterAudioProcessor::performFrequencyAnalysis() noexcept
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (analysisFifo.begin(), analysisFifo.end(), fftData.begin());

    windowingFunction.multiplyWithWindowingTable (fftData.data(), static_cast<size_t> (fftSize));
    forwardFFT.performFrequencyOnlyForwardTransform (fftData.data());

    const auto currentBufferIndex = activeSpectrumBufferIndex.load (std::memory_order_relaxed);
    const auto writeBufferIndex = 1 - currentBufferIndex;
    const auto& previousBuffer = spectrumBuffers[static_cast<size_t> (currentBufferIndex)];
    auto& writeBuffer = spectrumBuffers[static_cast<size_t> (writeBufferIndex)];
    const auto normalisation = 1.0f / static_cast<float> (fftSize);
    const auto alpha = spectrumSmoothingAlpha;
    const auto oneMinusAlpha = 1.0f - alpha;

    for (int bin = 0; bin < spectrumBinCount; ++bin)
    {
        const auto index = static_cast<size_t> (bin);
        const auto currentMagnitude = fftData[index] * normalisation;
        writeBuffer[index] = (alpha * currentMagnitude) + (oneMinusAlpha * previousBuffer[index]);
    }

    activeSpectrumBufferIndex.store (writeBufferIndex, std::memory_order_release);
}

void BolbolRefMasterAudioProcessor::updatePreviewFilterCoefficients (int numSamples) noexcept
{
    const auto previewMatchPoints = getPreviewMatchPoints();
    constexpr std::array<float, previewBandCount> bandQValues { 0.707f, 0.85f, 0.9f, 0.85f, 0.707f };

    for (size_t index = 0; index < previewFilters.size(); ++index)
    {
        const auto targetGainDb = hasReferenceTrack() ? previewMatchPoints[index].gainDb : 0.0f;
        previewBandGainSmoothers[index].setTargetValue (targetGainDb);
        const auto smoothedGainDb = previewBandGainSmoothers[index].skip (numSamples);
        currentPreviewBandGainsDb[index].store (smoothedGainDb, std::memory_order_release);
        const auto gainFactor = juce::Decibels::decibelsToGain (smoothedGainDb);

        std::array<float, 6> coefficients;

        if (index == 0)
        {
            coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (currentSampleRate,
                                                                                   60.0f,
                                                                                   bandQValues[index],
                                                                                   gainFactor);
        }
        else if (index == previewFilters.size() - 1)
        {
            coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (currentSampleRate,
                                                                                    12000.0f,
                                                                                    bandQValues[index],
                                                                                    gainFactor);
        }
        else
        {
            coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (currentSampleRate,
                                                                                     previewMatchPoints[index].frequencyHz,
                                                                                     juce::jmax (0.4f, previewMatchPoints[index].q),
                                                                                     gainFactor);
        }

        assignIirCoefficients (*previewFilters[index].state, coefficients);
    }
}

void BolbolRefMasterAudioProcessor::applyPreviewEq (juce::AudioBuffer<float>& buffer) noexcept
{
    const auto targetWetMix = (hasReferenceTrack() && isPreviewEqEnabled() && ! isPreviewEqBypassed()) ? 1.0f : 0.0f;
    previewWetMixSmoother.setTargetValue (targetWetMix);

    if (previewWetMixSmoother.getCurrentValue() <= 0.0f && targetWetMix <= 0.0f)
        return;

    if (buffer.getNumChannels() > previewEqBuffer.getNumChannels()
        || buffer.getNumSamples() > previewEqBuffer.getNumSamples())
        return;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        previewEqBuffer.copyFrom (channel, 0, buffer, channel, 0, buffer.getNumSamples());

    juce::dsp::AudioBlock<float> block (previewEqBuffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    for (auto& previewFilter : previewFilters)
        previewFilter.process (context);

    previewEqBuffer.applyGain (juce::Decibels::decibelsToGain (getPreviewOutputTrimDb()));

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto wetMix = previewWetMixSmoother.getNextValue();
        const auto dryMix = 1.0f - wetMix;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto drySample = buffer.getSample (channel, sample);
            const auto wetSample = previewEqBuffer.getSample (channel, sample);
            buffer.setSample (channel, sample, (dryMix * drySample) + (wetMix * wetSample));
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BolbolRefMasterAudioProcessor();
}
