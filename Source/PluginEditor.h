/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <array>

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class BolbolRefMasterAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                             public juce::FileDragAndDropTarget,
                                             private juce::Timer
{
public:
    BolbolRefMasterAudioProcessorEditor (BolbolRefMasterAudioProcessor&);
    ~BolbolRefMasterAudioProcessorEditor() override;

    //==============================================================================
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawSpectrumAnalyzer (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawBandSummary (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawDetailedSummary (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawLegend (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    juce::Path createSpectrumPath (juce::Rectangle<float> bounds,
                                   const std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount>& spectrum) const;
    void drawSpectrumScale (juce::Graphics& g, juce::Rectangle<float> bounds) const;
    void drawPreviewMatchPoints (juce::Graphics& g, juce::Rectangle<float> bounds) const;
    float calculateSpectrumNormalisationDb (
        const std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount>& spectrum,
        double sampleRate) const;
    float getNormalisedSpectrumDb (
        const std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount>& spectrum,
        int bin,
        float normalisationDb) const;
    float calculateSpectrumCorrelation() const;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    BolbolRefMasterAudioProcessor& audioProcessor;
    juce::Slider previewBlendSlider;
    juce::ToggleButton previewEqToggle;
    juce::ToggleButton previewBypassToggle;
    std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount> displaySpectrum {};
    std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount> displayReferenceSpectrum {};
    std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount> displayTargetPreviewSpectrum {};
    std::unique_ptr<juce::FileChooser> referenceFileChooser;
    juce::Rectangle<int> referenceCardBounds;
    juce::Rectangle<int> clearReferenceButtonBounds;
    juce::Rectangle<int> simpleTabBounds;
    juce::Rectangle<int> detailedTabBounds;
    bool showDetailedComparison = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BolbolRefMasterAudioProcessorEditor)
};
