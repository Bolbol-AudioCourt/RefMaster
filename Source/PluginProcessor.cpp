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
    fifoIndex = 0;
    activeSpectrumBufferIndex.store (0, std::memory_order_release);
    activeReferenceSpectrumBufferIndex.store (0, std::memory_order_release);

    analysisFifo.fill (0.0f);
    fftData.fill (0.0f);

    for (auto& spectrumBuffer : spectrumBuffers)
        spectrumBuffer.fill (0.0f);

    for (auto& referenceSpectrumBuffer : referenceSpectrumBuffers)
        referenceSpectrumBuffer.fill (0.0f);
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
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void BolbolRefMasterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
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
        differenceSpectrum[index] = referenceDb - inputDb;
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
    referenceTrackLoaded.store (false, std::memory_order_release);
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

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BolbolRefMasterAudioProcessor();
}
