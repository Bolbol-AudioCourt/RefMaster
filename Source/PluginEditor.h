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
                                             private juce::Timer
{
public:
    BolbolRefMasterAudioProcessorEditor (BolbolRefMasterAudioProcessor&);
    ~BolbolRefMasterAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawSpectrumAnalyzer (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawLegend (juce::Graphics& g, juce::Rectangle<int> bounds) const;
    juce::Path createSpectrumPath (juce::Rectangle<float> bounds) const;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    BolbolRefMasterAudioProcessor& audioProcessor;
    std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount> displaySpectrum {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BolbolRefMasterAudioProcessorEditor)
};
