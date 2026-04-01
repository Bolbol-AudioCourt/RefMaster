/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cstring>

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

    analysisFifo.fill (0.0f);
    fftData.fill (0.0f);

    for (auto& spectrumBuffer : spectrumBuffers)
        spectrumBuffer.fill (0.0f);
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
