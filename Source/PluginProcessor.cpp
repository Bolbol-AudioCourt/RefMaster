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
using Processor = BolbolRefMasterAudioProcessor;
using SpectrumArray = std::array<float, Processor::spectrumBinCount>;
using GeneratedBandArray = std::array<Processor::GeneratedPreviewBand, Processor::previewBandCount>;
using MatchStateSnapshot = Processor::MatchStateSnapshot;

struct MatchCandidate
{
    float frequencyHz = 0.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
    float score = 0.0f;
};

constexpr std::array<float, Processor::previewBandCount> defaultPreviewBandFrequencies {{
    60.0f, 180.0f, 900.0f, 4200.0f, 12000.0f
}};

constexpr auto appliedMatchStateType = "APPLIED_MATCH_STATE";
constexpr auto appliedMatchBandType = "BAND";

juce::String formatReferenceDuration (double seconds)
{
    const auto totalSeconds = juce::jmax (0, juce::roundToInt (seconds));
    const auto minutes = totalSeconds / 60;
    const auto remainingSeconds = totalSeconds % 60;
    return juce::String (minutes) + ":" + juce::String (remainingSeconds).paddedLeft ('0', 2);
}

float calculateSpectrumAverageDb (
    const SpectrumArray& spectrum,
    double sampleRate)
{
    const auto minFrequency = 20.0f;
    const auto maxFrequency = juce::jmin (20000.0f, static_cast<float> (sampleRate * 0.5));
    const auto binWidth = static_cast<float> (sampleRate / Processor::fftSize);

    float sumDb = 0.0f;
    int count = 0;

    for (int bin = 1; bin < Processor::spectrumBinCount; ++bin)
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
    destination = source;
}

juce::AudioParameterBool* getBoolParameter (juce::AudioProcessorValueTreeState& parameters, const juce::String& id)
{
    return dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (id));
}

juce::AudioParameterFloat* getFloatParameter (juce::AudioProcessorValueTreeState& parameters, const juce::String& id)
{
    return dynamic_cast<juce::AudioParameterFloat*> (parameters.getParameter (id));
}

int frequencyToBin (float frequencyHz, double sampleRate)
{
    const auto binWidth = static_cast<float> (sampleRate / Processor::fftSize);
    return juce::jlimit (1, Processor::spectrumBinCount - 1, juce::roundToInt (frequencyHz / juce::jmax (binWidth, 1.0e-3f)));
}

float binToFrequency (int bin, double sampleRate)
{
    return static_cast<float> (bin) * static_cast<float> (sampleRate / Processor::fftSize);
}

GeneratedBandArray makeDefaultGeneratedBands() noexcept
{
    GeneratedBandArray bands {};

    for (size_t index = 0; index < bands.size(); ++index)
    {
        auto shape = Processor::GeneratedPreviewBand::Shape::peak;

        if (index == 0)
            shape = Processor::GeneratedPreviewBand::Shape::lowShelf;
        else if (index == bands.size() - 1)
            shape = Processor::GeneratedPreviewBand::Shape::highShelf;

        bands[index] = Processor::GeneratedPreviewBand {
            shape,
            defaultPreviewBandFrequencies[index],
            0.0f,
            index == 0 || index == bands.size() - 1 ? 0.707f : 1.0f
        };
    }

    return bands;
}

float calculateBandQFromWidth (const SpectrumArray& differenceSpectrum,
                               int centreBin,
                               float lowHz,
                               float highHz,
                               double sampleRate) noexcept
{
    if (centreBin <= 0)
        return 1.0f;

    const auto centreValue = differenceSpectrum[static_cast<size_t> (centreBin)];
    const auto magnitude = std::abs (centreValue);

    if (magnitude < 1.0e-4f)
        return 1.0f;

    const auto halfMagnitude = magnitude * 0.5f;
    const auto sign = centreValue >= 0.0f ? 1.0f : -1.0f;
    int leftBin = centreBin;
    int rightBin = centreBin;

    while (leftBin > 1)
    {
        const auto value = differenceSpectrum[static_cast<size_t> (leftBin - 1)];
        const auto frequency = binToFrequency (leftBin - 1, sampleRate);

        if (frequency < lowHz || std::abs (value) < halfMagnitude || value * sign <= 0.0f)
            break;

        --leftBin;
    }

    while (rightBin < Processor::spectrumBinCount - 1)
    {
        const auto value = differenceSpectrum[static_cast<size_t> (rightBin + 1)];
        const auto frequency = binToFrequency (rightBin + 1, sampleRate);

        if (frequency >= highHz || std::abs (value) < halfMagnitude || value * sign <= 0.0f)
            break;

        ++rightBin;
    }

    const auto leftFrequency = juce::jmax (lowHz, binToFrequency (leftBin, sampleRate));
    const auto rightFrequency = juce::jmin (highHz, binToFrequency (rightBin, sampleRate));
    const auto bandwidth = juce::jmax (30.0f, rightFrequency - leftFrequency);
    const auto centreFrequency = juce::jmax (30.0f, binToFrequency (centreBin, sampleRate));
    return juce::jlimit (0.45f, 4.5f, centreFrequency / bandwidth);
}

Processor::GeneratedPreviewBand createShelfBand (Processor::GeneratedPreviewBand::Shape shape,
                                                 const SpectrumArray& differenceSpectrum,
                                                 double sampleRate,
                                                 float lowHz,
                                                 float highHz,
                                                 float defaultFrequencyHz) noexcept
{
    auto band = makeDefaultGeneratedBands()[shape == Processor::GeneratedPreviewBand::Shape::lowShelf ? 0 : Processor::previewBandCount - 1];
    const auto lowBin = frequencyToBin (lowHz, sampleRate);
    const auto highBin = frequencyToBin (highHz, sampleRate);

    float sum = 0.0f;
    int count = 0;
    float strongestValue = 0.0f;
    int strongestBin = 0;

    for (int bin = lowBin; bin <= highBin; ++bin)
    {
        const auto value = differenceSpectrum[static_cast<size_t> (bin)];
        sum += value;
        ++count;

        if (std::abs (value) > std::abs (strongestValue))
        {
            strongestValue = value;
            strongestBin = bin;
        }
    }

    const auto averageValue = count > 0 ? (sum / static_cast<float> (count)) : 0.0f;
    const auto blendedGain = ((averageValue * 0.65f) + (strongestValue * 0.35f)) * 0.75f;

    band.shape = shape;
    band.frequencyHz = strongestBin > 0 ? binToFrequency (strongestBin, sampleRate) : defaultFrequencyHz;
    band.gainDb = juce::jlimit (-3.5f, 3.5f, blendedGain);
    band.q = 0.707f;

    if (std::abs (band.gainDb) < 0.75f)
        band.gainDb = 0.0f;

    return band;
}

std::array<MatchCandidate, 3> selectPeakBands (const SpectrumArray& differenceSpectrum, double sampleRate) noexcept
{
    constexpr int maxCandidates = 16;
    std::array<MatchCandidate, maxCandidates> rankedCandidates {};
    int rankedCount = 0;

    const auto lowBin = frequencyToBin (100.0f, sampleRate);
    const auto highBin = frequencyToBin (12000.0f, sampleRate);

    for (int bin = lowBin + 2; bin < highBin - 2; ++bin)
    {
        const auto value = differenceSpectrum[static_cast<size_t> (bin)];
        const auto magnitude = std::abs (value);

        if (magnitude < 0.9f)
            continue;

        const auto localMax = value > 0.0f
                           && value >= differenceSpectrum[static_cast<size_t> (bin - 1)]
                           && value >= differenceSpectrum[static_cast<size_t> (bin - 2)]
                           && value >= differenceSpectrum[static_cast<size_t> (bin + 1)]
                           && value >= differenceSpectrum[static_cast<size_t> (bin + 2)];
        const auto localMin = value < 0.0f
                           && value <= differenceSpectrum[static_cast<size_t> (bin - 1)]
                           && value <= differenceSpectrum[static_cast<size_t> (bin - 2)]
                           && value <= differenceSpectrum[static_cast<size_t> (bin + 1)]
                           && value <= differenceSpectrum[static_cast<size_t> (bin + 2)];

        if (! localMax && ! localMin)
            continue;

        MatchCandidate candidate;
        candidate.frequencyHz = binToFrequency (bin, sampleRate);
        candidate.gainDb = juce::jlimit (-4.5f, 4.5f, value * 0.75f);
        candidate.q = calculateBandQFromWidth (differenceSpectrum, bin, 100.0f, 12000.0f, sampleRate);
        candidate.score = std::abs (candidate.gainDb);

        int insertIndex = rankedCount;

        for (int i = 0; i < rankedCount; ++i)
        {
            if (candidate.score > rankedCandidates[static_cast<size_t> (i)].score)
            {
                insertIndex = i;
                break;
            }
        }

        if (insertIndex < maxCandidates)
        {
            const auto shiftEnd = juce::jmin (rankedCount, maxCandidates - 1);

            for (int i = shiftEnd; i > insertIndex; --i)
                rankedCandidates[static_cast<size_t> (i)] = rankedCandidates[static_cast<size_t> (i - 1)];

            rankedCandidates[static_cast<size_t> (insertIndex)] = candidate;
            rankedCount = juce::jmin (rankedCount + 1, maxCandidates);
        }
    }

    std::array<MatchCandidate, 3> selected {};
    int selectedCount = 0;

    for (int i = 0; i < rankedCount && selectedCount < static_cast<int> (selected.size()); ++i)
    {
        const auto& candidate = rankedCandidates[static_cast<size_t> (i)];
        bool isTooClose = false;

        for (int selectedIndex = 0; selectedIndex < selectedCount; ++selectedIndex)
        {
            const auto octaveDistance = std::abs (std::log2 (candidate.frequencyHz / selected[static_cast<size_t> (selectedIndex)].frequencyHz));

            if (octaveDistance < 0.55f)
            {
                isTooClose = true;
                break;
            }
        }

        if (isTooClose)
            continue;

        selected[static_cast<size_t> (selectedCount++)] = candidate;
    }

    constexpr std::array<std::pair<float, float>, 3> fallbackRanges {{
        { 120.0f, 400.0f },
        { 400.0f, 2500.0f },
        { 2500.0f, 9000.0f },
    }};

    for (const auto& [rangeLowHz, rangeHighHz] : fallbackRanges)
    {
        if (selectedCount >= static_cast<int> (selected.size()))
            break;

        const auto rangeLowBin = frequencyToBin (rangeLowHz, sampleRate);
        const auto rangeHighBin = frequencyToBin (rangeHighHz, sampleRate);
        float strongestValue = 0.0f;
        int strongestBin = 0;

        for (int bin = rangeLowBin; bin <= rangeHighBin; ++bin)
        {
            const auto value = differenceSpectrum[static_cast<size_t> (bin)];

            if (std::abs (value) > std::abs (strongestValue))
            {
                strongestValue = value;
                strongestBin = bin;
            }
        }

        if (strongestBin <= 0)
            continue;

        MatchCandidate candidate;
        candidate.frequencyHz = binToFrequency (strongestBin, sampleRate);
        candidate.gainDb = juce::jlimit (-3.5f, 3.5f, strongestValue * 0.7f);
        candidate.q = calculateBandQFromWidth (differenceSpectrum, strongestBin, rangeLowHz, rangeHighHz, sampleRate);
        candidate.score = std::abs (candidate.gainDb);

        if (candidate.score < 0.6f)
            continue;

        bool isTooClose = false;

        for (int selectedIndex = 0; selectedIndex < selectedCount; ++selectedIndex)
        {
            const auto octaveDistance = std::abs (std::log2 (candidate.frequencyHz / selected[static_cast<size_t> (selectedIndex)].frequencyHz));

            if (octaveDistance < 0.45f)
            {
                isTooClose = true;
                break;
            }
        }

        if (! isTooClose)
            selected[static_cast<size_t> (selectedCount++)] = candidate;
    }

    std::sort (selected.begin(), selected.end(), [] (const MatchCandidate& left, const MatchCandidate& right)
    {
        const auto leftValid = left.frequencyHz > 0.0f;
        const auto rightValid = right.frequencyHz > 0.0f;

        if (leftValid != rightValid)
            return leftValid > rightValid;

        return left.frequencyHz < right.frequencyHz;
    });

    return selected;
}

GeneratedBandArray buildGeneratedBandsFromDifferenceSpectrum (const SpectrumArray& differenceSpectrum, double sampleRate) noexcept
{
    auto bands = makeDefaultGeneratedBands();
    bands[0] = createShelfBand (Processor::GeneratedPreviewBand::Shape::lowShelf,
                                differenceSpectrum,
                                sampleRate,
                                20.0f,
                                160.0f,
                                defaultPreviewBandFrequencies[0]);
    bands[bands.size() - 1] = createShelfBand (Processor::GeneratedPreviewBand::Shape::highShelf,
                                               differenceSpectrum,
                                               sampleRate,
                                               7000.0f,
                                               juce::jmin (20000.0f, static_cast<float> (sampleRate * 0.48)),
                                               defaultPreviewBandFrequencies[bands.size() - 1]);

    const auto selectedPeaks = selectPeakBands (differenceSpectrum, sampleRate);

    for (size_t index = 0; index < selectedPeaks.size(); ++index)
    {
        bands[index + 1] = Processor::GeneratedPreviewBand {
            Processor::GeneratedPreviewBand::Shape::peak,
            selectedPeaks[index].frequencyHz > 0.0f ? selectedPeaks[index].frequencyHz : defaultPreviewBandFrequencies[index + 1],
            selectedPeaks[index].gainDb,
            selectedPeaks[index].frequencyHz > 0.0f ? selectedPeaks[index].q : 1.0f
        };
    }

    return bands;
}

float calculateOutputTrimFromBands (const GeneratedBandArray& bands) noexcept
{
    float weightedSum = 0.0f;
    float positiveBoostSum = 0.0f;
    float totalWeight = 0.0f;

    for (size_t index = 0; index < bands.size(); ++index)
    {
        const auto weight = index == 0 || index == bands.size() - 1 ? 0.75f : 1.0f;
        weightedSum += bands[index].gainDb * weight;
        positiveBoostSum += juce::jmax (0.0f, bands[index].gainDb) * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0f)
        return 0.0f;

    const auto averageGainDb = weightedSum / totalWeight;
    const auto averagePositiveBoostDb = positiveBoostSum / totalWeight;
    return juce::jlimit (-6.0f, 3.0f, (-averageGainDb * 0.35f) - (averagePositiveBoostDb * 0.55f));
}

juce::String shapeToString (Processor::GeneratedPreviewBand::Shape shape)
{
    switch (shape)
    {
        case Processor::GeneratedPreviewBand::Shape::lowShelf:  return "lowShelf";
        case Processor::GeneratedPreviewBand::Shape::highShelf: return "highShelf";
        case Processor::GeneratedPreviewBand::Shape::peak:      return "peak";
    }

    return "peak";
}

Processor::GeneratedPreviewBand::Shape shapeFromString (const juce::String& text)
{
    if (text == "lowShelf")
        return Processor::GeneratedPreviewBand::Shape::lowShelf;

    if (text == "highShelf")
        return Processor::GeneratedPreviewBand::Shape::highShelf;

    return Processor::GeneratedPreviewBand::Shape::peak;
}

juce::ValueTree createAppliedMatchStateTree (const MatchStateSnapshot& snapshot)
{
    juce::ValueTree tree (appliedMatchStateType);
    tree.setProperty ("outputTrimDb", snapshot.outputTrimDb, nullptr);

    for (const auto& band : snapshot.bands)
    {
        juce::ValueTree bandTree (appliedMatchBandType);
        bandTree.setProperty ("shape", shapeToString (band.shape), nullptr);
        bandTree.setProperty ("frequencyHz", band.frequencyHz, nullptr);
        bandTree.setProperty ("gainDb", band.gainDb, nullptr);
        bandTree.setProperty ("q", band.q, nullptr);
        tree.addChild (bandTree, -1, nullptr);
    }

    return tree;
}

MatchStateSnapshot parseAppliedMatchStateTree (const juce::ValueTree& tree)
{
    MatchStateSnapshot snapshot;

    if (! tree.isValid() || ! tree.hasType (appliedMatchStateType))
        return snapshot;

    snapshot.outputTrimDb = static_cast<float> (tree.getProperty ("outputTrimDb", 0.0f));
    snapshot.bands = makeDefaultGeneratedBands();
    const auto childCount = juce::jmin (tree.getNumChildren(), Processor::previewBandCount);

    for (int index = 0; index < childCount; ++index)
    {
        const auto bandTree = tree.getChild (index);

        if (! bandTree.hasType (appliedMatchBandType))
            continue;

        snapshot.bands[static_cast<size_t> (index)] = Processor::GeneratedPreviewBand {
            shapeFromString (bandTree.getProperty ("shape").toString()),
            static_cast<float> (bandTree.getProperty ("frequencyHz", defaultPreviewBandFrequencies[static_cast<size_t> (index)])),
            static_cast<float> (bandTree.getProperty ("gainDb", 0.0f)),
            static_cast<float> (bandTree.getProperty ("q", 1.0f)),
        };
    }

    snapshot.valid = childCount > 0;
    return snapshot;
}
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BolbolRefMasterAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterBool> (appliedMatchEnabledParamID, "Applied Match Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (previewEqEnabledParamID, "Preview EQ Enabled", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (previewEqBypassedParamID, "Preview EQ Bypassed", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (previewBlendAmountParamID,
                                                             "Preview Blend Amount",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (previewOutputGainParamID,
                                                             "Preview Output Gain",
                                                             juce::NormalisableRange<float> (-12.0f, 12.0f),
                                                             0.0f));

    return layout;
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
    , parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
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

    analysisFifo.fill (0.0f);
    fftData.fill (0.0f);

    for (auto& spectrumBuffer : spectrumBuffers)
        spectrumBuffer.fill (0.0f);

    if (! hasReferenceTrack())
    {
        activeReferenceSpectrumBufferIndex.store (0, std::memory_order_release);

        for (auto& referenceSpectrumBuffer : referenceSpectrumBuffers)
            referenceSpectrumBuffer.fill (0.0f);
    }

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
    auto state = parameters.copyState();
    state.setProperty ("referenceTrackPath", referenceTrackLoaded.load (std::memory_order_acquire) ? referenceTrackPath : juce::String(), nullptr);

    if (const auto committedMatchState = getCommittedMatchState(); committedMatchState.valid)
        state.addChild (createAppliedMatchStateTree (committedMatchState), -1, nullptr);

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
        const auto referencePath = state.getProperty ("referenceTrackPath").toString();
        const auto committedMatchTree = state.getChildWithName (appliedMatchStateType);

        state.removeProperty ("referenceTrackPath", nullptr);
        if (committedMatchTree.isValid())
            state.removeChild (committedMatchTree, nullptr);

        parameters.replaceState (state);

        if (committedMatchTree.isValid())
            setCommittedMatchState (parseAppliedMatchStateTree (committedMatchTree));
        else
            clearCommittedMatchStateBuffers();

        if (referencePath.isNotEmpty())
            loadReferenceFile (juce::File (referencePath));
        else
            clearReferenceTrack();
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
    const auto previewBands = getGeneratedPreviewBands();

    for (size_t index = 0; index < matchPoints.size(); ++index)
        matchPoints[index] = PreviewMatchPoint { previewBands[index].frequencyHz, previewBands[index].gainDb, previewBands[index].q };

    return matchPoints;
}

std::array<BolbolRefMasterAudioProcessor::GeneratedPreviewBand, BolbolRefMasterAudioProcessor::previewBandCount>
BolbolRefMasterAudioProcessor::getGeneratedPreviewBands() const noexcept
{
    return buildLivePreviewMatchState().bands;
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
    return buildLivePreviewMatchState().outputTrimDb;
}

void BolbolRefMasterAudioProcessor::commitPreviewAsAppliedMatch()
{
    const auto previewState = buildLivePreviewMatchState();

    if (! previewState.valid)
    {
        clearAppliedMatch();
        return;
    }

    setCommittedMatchState (previewState);
    setAppliedMatchEnabled (true);
    setPreviewEqEnabled (false);
    setPreviewEqBypassed (false);
}

void BolbolRefMasterAudioProcessor::clearAppliedMatch() noexcept
{
    clearCommittedMatchStateBuffers();
    setAppliedMatchEnabled (false);
}

bool BolbolRefMasterAudioProcessor::hasAppliedMatch() const noexcept
{
    return committedMatchAvailable.load (std::memory_order_acquire);
}

void BolbolRefMasterAudioProcessor::setAppliedMatchEnabled (bool shouldBeEnabled) noexcept
{
    if (auto* parameter = getBoolParameter (parameters, appliedMatchEnabledParamID))
        parameter->setValueNotifyingHost (shouldBeEnabled ? 1.0f : 0.0f);
}

bool BolbolRefMasterAudioProcessor::isAppliedMatchEnabled() const noexcept
{
    if (! hasAppliedMatch())
        return false;

    if (const auto* value = parameters.getRawParameterValue (appliedMatchEnabledParamID))
        return value->load() >= 0.5f;

    return false;
}

void BolbolRefMasterAudioProcessor::setPreviewOutputGainDb (float newGainDb) noexcept
{
    if (auto* parameter = getFloatParameter (parameters, previewOutputGainParamID))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (-12.0f, 12.0f, newGainDb)));
}

float BolbolRefMasterAudioProcessor::getPreviewOutputGainDb() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue (previewOutputGainParamID))
        return value->load();

    return 0.0f;
}

void BolbolRefMasterAudioProcessor::setPreviewBlendAmount (float newAmount) noexcept
{
    if (auto* parameter = getFloatParameter (parameters, previewBlendAmountParamID))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (juce::jlimit (0.0f, 1.0f, newAmount)));
}

float BolbolRefMasterAudioProcessor::getPreviewBlendAmount() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue (previewBlendAmountParamID))
        return value->load();

    return 0.5f;
}

void BolbolRefMasterAudioProcessor::setPreviewEqEnabled (bool shouldBeEnabled) noexcept
{
    if (auto* parameter = getBoolParameter (parameters, previewEqEnabledParamID))
        parameter->setValueNotifyingHost (shouldBeEnabled ? 1.0f : 0.0f);
}

bool BolbolRefMasterAudioProcessor::isPreviewEqEnabled() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue (previewEqEnabledParamID))
        return value->load() >= 0.5f;

    return false;
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
    if (auto* parameter = getBoolParameter (parameters, previewEqBypassedParamID))
        parameter->setValueNotifyingHost (shouldBeBypassed ? 1.0f : 0.0f);
}

bool BolbolRefMasterAudioProcessor::isPreviewEqBypassed() const noexcept
{
    if (const auto* value = parameters.getRawParameterValue (previewEqBypassedParamID))
        return value->load() >= 0.5f;

    return false;
}

BolbolRefMasterAudioProcessor::MatchStateSnapshot BolbolRefMasterAudioProcessor::buildLivePreviewMatchState() const noexcept
{
    MatchStateSnapshot snapshot;

    if (! hasReferenceTrack())
    {
        snapshot.bands = makeDefaultGeneratedBands();
        return snapshot;
    }

    const auto differenceSpectrum = getPreviewDifferenceSpectrumDb();
    snapshot.bands = buildGeneratedBandsFromDifferenceSpectrum (differenceSpectrum, juce::jmax (getSampleRate(), 44100.0));
    snapshot.outputTrimDb = calculateOutputTrimFromBands (snapshot.bands);
    snapshot.valid = true;
    return snapshot;
}

BolbolRefMasterAudioProcessor::MatchStateSnapshot BolbolRefMasterAudioProcessor::getCommittedMatchState() const noexcept
{
    if (! hasAppliedMatch())
        return {};

    return committedMatchStateBuffers[static_cast<size_t> (activeCommittedMatchStateBufferIndex.load (std::memory_order_acquire))];
}

BolbolRefMasterAudioProcessor::MatchStateSnapshot BolbolRefMasterAudioProcessor::getActiveMatchState() const noexcept
{
    if (isPreviewEqBypassed())
        return {};

    if (isAppliedMatchEnabled())
        return getCommittedMatchState();

    if (isPreviewEqActive())
        return buildLivePreviewMatchState();

    return {};
}

void BolbolRefMasterAudioProcessor::setCommittedMatchState (const MatchStateSnapshot& snapshot) noexcept
{
    const auto writeIndex = 1 - activeCommittedMatchStateBufferIndex.load (std::memory_order_relaxed);
    committedMatchStateBuffers[static_cast<size_t> (writeIndex)] = snapshot;
    activeCommittedMatchStateBufferIndex.store (writeIndex, std::memory_order_release);
    committedMatchAvailable.store (snapshot.valid, std::memory_order_release);
}

void BolbolRefMasterAudioProcessor::clearCommittedMatchStateBuffers() noexcept
{
    committedMatchStateBuffers[0] = {};
    committedMatchStateBuffers[1] = {};
    activeCommittedMatchStateBufferIndex.store (0, std::memory_order_release);
    committedMatchAvailable.store (false, std::memory_order_release);
}

bool BolbolRefMasterAudioProcessor::loadReferenceFile (const juce::File& file)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader> (audioFormatManager.createReaderFor (file));

    if (reader == nullptr)
    {
        clearReferenceTrack();
        referenceTrackName = file.getFileName();
        referenceTrackInfo = "Failed to load reference";
        referenceTrackPath = file.getFullPathName();
        referenceLoadError.store (true, std::memory_order_release);
        return false;
    }

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
    referenceLoadError.store (false, std::memory_order_release);
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
    referenceLoadError.store (false, std::memory_order_release);
    setPreviewEqEnabled (false);
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

bool BolbolRefMasterAudioProcessor::hasReferenceLoadError() const noexcept
{
    return referenceLoadError.load (std::memory_order_acquire);
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
    const auto activeMatchState = getActiveMatchState();
    const auto activeBands = activeMatchState.valid ? activeMatchState.bands : makeDefaultGeneratedBands();
    constexpr std::array<float, previewBandCount> bandQValues { 0.707f, 0.85f, 0.9f, 0.85f, 0.707f };

    for (size_t index = 0; index < previewFilters.size(); ++index)
    {
        const auto targetGainDb = activeMatchState.valid ? activeBands[index].gainDb : 0.0f;
        previewBandGainSmoothers[index].setTargetValue (targetGainDb);
        const auto smoothedGainDb = previewBandGainSmoothers[index].skip (numSamples);
        currentPreviewBandGainsDb[index].store (smoothedGainDb, std::memory_order_release);
        const auto gainFactor = juce::Decibels::decibelsToGain (smoothedGainDb);

        std::array<float, 6> coefficients;

        if (activeBands[index].shape == GeneratedPreviewBand::Shape::lowShelf)
        {
            coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeLowShelf (currentSampleRate,
                                                                                   juce::jlimit (20.0f, 180.0f, activeBands[index].frequencyHz),
                                                                                   bandQValues[index],
                                                                                   gainFactor);
        }
        else if (activeBands[index].shape == GeneratedPreviewBand::Shape::highShelf)
        {
            coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makeHighShelf (currentSampleRate,
                                                                                    juce::jlimit (5000.0f, 20000.0f, activeBands[index].frequencyHz),
                                                                                    bandQValues[index],
                                                                                    gainFactor);
        }
        else
        {
            coefficients = juce::dsp::IIR::ArrayCoefficients<float>::makePeakFilter (currentSampleRate,
                                                                                     juce::jlimit (80.0f, static_cast<float> (currentSampleRate * 0.45), activeBands[index].frequencyHz),
                                                                                     juce::jmax (0.45f, activeBands[index].q),
                                                                                     gainFactor);
        }

        assignIirCoefficients (*previewFilters[index].state, coefficients);
    }
}

void BolbolRefMasterAudioProcessor::applyPreviewEq (juce::AudioBuffer<float>& buffer) noexcept
{
    const auto activeMatchState = getActiveMatchState();
    const auto targetWetMix = activeMatchState.valid ? 1.0f : 0.0f;
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

    const auto activeTrimDb = activeMatchState.valid ? activeMatchState.outputTrimDb : 0.0f;
    previewEqBuffer.applyGain (juce::Decibels::decibelsToGain (activeTrimDb + getPreviewOutputGainDb()));

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
