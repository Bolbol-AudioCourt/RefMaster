/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace
{
const auto backgroundColour = juce::Colour (0xff17181d);
const auto panelColour = juce::Colour (0xff1c1d24);
const auto graphColour = juce::Colour (0xff15161c);
const auto borderColour = juce::Colour (0x33ffffff);
const auto textColour = juce::Colour (0xffd9d3ea);
const auto mutedTextColour = juce::Colour (0x66d9d3ea);
const auto accentColour = juce::Colour (0xffc7b7ff);
const auto fillColour = juce::Colour (0x663f5665);
const auto legendColour = juce::Colour (0xff6ac18d);
const auto scaleLineColour = juce::Colour (0x18ffffff);
const auto labelColour = juce::Colour (0x88d9d3ea);
}

//==============================================================================
BolbolRefMasterAudioProcessorEditor::BolbolRefMasterAudioProcessorEditor (BolbolRefMasterAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setSize (1024, 720);
    startTimerHz (30);
}

BolbolRefMasterAudioProcessorEditor::~BolbolRefMasterAudioProcessorEditor()
{
}

//==============================================================================
void BolbolRefMasterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (58);

    g.setColour (juce::Colour (0xff121318));
    g.fillRect (header);

    g.setColour (textColour);
    g.setFont (juce::FontOptions (28.0f));
    g.drawText ("BOLBOL REFMASTER", header.removeFromLeft (420).reduced (20, 10), juce::Justification::centredLeft);

    auto content = bounds.reduced (20, 18);
    auto sidebar = content.removeFromRight (260);
    auto analyzerArea = content;

    drawSpectrumAnalyzer (g, analyzerArea);

    g.setColour (panelColour);
    g.fillRoundedRectangle (sidebar.toFloat(), 14.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (sidebar.toFloat(), 14.0f, 1.0f);

    auto sidebarContent = sidebar.reduced (18);
    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (18.0f));
    g.drawText ("ANALYZER", sidebarContent.removeFromTop (28), juce::Justification::centredLeft);

    g.setColour (textColour);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText ("Real-time input FFT", sidebarContent.removeFromTop (28), juce::Justification::centredLeft, 1);

    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText ("Source: stereo average", sidebarContent.removeFromTop (22), juce::Justification::centredLeft, 1);
    g.drawFittedText ("FFT size: 2048", sidebarContent.removeFromTop (22), juce::Justification::centredLeft, 1);
    g.drawFittedText ("Update: 30 fps", sidebarContent.removeFromTop (22), juce::Justification::centredLeft, 1);

    auto statusArea = sidebar.removeFromBottom (92).reduced (18, 14);
    g.setColour (juce::Colour (0xff111217));
    g.fillRoundedRectangle (statusArea.toFloat(), 12.0f);
    g.setColour (mutedTextColour);
    g.drawRoundedRectangle (statusArea.toFloat(), 12.0f, 1.0f);
    g.drawText ("Next", statusArea.removeFromTop (24), juce::Justification::centredLeft);

    g.setColour (textColour);
    g.drawFittedText ("Reference import, smoothing, and EQ matching come after the analyzer is stable.", statusArea, juce::Justification::topLeft, 3);
}

void BolbolRefMasterAudioProcessorEditor::resized()
{
}

void BolbolRefMasterAudioProcessorEditor::timerCallback()
{
    const auto latestSpectrum = audioProcessor.getLatestMagnitudeSpectrum();

    for (size_t i = 0; i < displaySpectrum.size(); ++i)
        displaySpectrum[i] = juce::jmax (latestSpectrum[i], displaySpectrum[i] * 0.82f);

    repaint();
}

void BolbolRefMasterAudioProcessorEditor::drawSpectrumAnalyzer (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    auto panelBounds = bounds.toFloat();

    g.setColour (panelColour);
    g.fillRoundedRectangle (panelBounds, 16.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (panelBounds, 16.0f, 1.0f);

    auto content = bounds.reduced (20);
    auto titleRow = content.removeFromTop (28);

    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (18.0f));
    g.drawText ("SPECTRUM ANALYZER", titleRow, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("FFT 2048 • input view", titleRow, juce::Justification::centredRight);

    auto legendArea = content.removeFromBottom (28);
    auto graphBounds = content.toFloat().reduced (0.0f, 12.0f);
    auto plotBounds = graphBounds.reduced (14.0f, 12.0f);

    g.setColour (graphColour);
    g.fillRoundedRectangle (graphBounds, 12.0f);

    drawSpectrumScale (g, plotBounds);

    auto spectrumPath = createSpectrumPath (plotBounds);

    juce::Path fillPath (spectrumPath);
    fillPath.lineTo (graphBounds.getRight() - 14.0f, graphBounds.getBottom() - 12.0f);
    fillPath.lineTo (graphBounds.getX() + 14.0f, graphBounds.getBottom() - 12.0f);
    fillPath.closeSubPath();

    juce::ColourGradient gradient (fillColour.brighter (0.4f),
                                   graphBounds.getCentreX(), graphBounds.getY(),
                                   fillColour.darker (0.8f),
                                   graphBounds.getCentreX(), graphBounds.getBottom(),
                                   false);
    g.setGradientFill (gradient);
    g.fillPath (fillPath);

    g.setColour (accentColour);
    g.strokePath (spectrumPath, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    drawLegend (g, legendArea);
}

void BolbolRefMasterAudioProcessorEditor::drawLegend (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    auto itemBounds = bounds.removeFromLeft (180);
    auto lineBounds = itemBounds.removeFromLeft (28).toFloat().withTrimmedTop (12.0f).withHeight (3.0f);

    g.setColour (accentColour);
    g.fillRoundedRectangle (lineBounds, 1.5f);

    g.setColour (textColour);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("Input spectrum", itemBounds, juce::Justification::centredLeft);

    auto secondaryBounds = bounds.removeFromLeft (220);
    auto secondaryLine = secondaryBounds.removeFromLeft (28).toFloat().withTrimmedTop (12.0f).withHeight (3.0f);
    g.setColour (legendColour);
    g.fillRoundedRectangle (secondaryLine, 1.5f);
    g.setColour (mutedTextColour);
    g.drawText ("UI guide inspired by docs", secondaryBounds, juce::Justification::centredLeft);
}

juce::Path BolbolRefMasterAudioProcessorEditor::createSpectrumPath (juce::Rectangle<float> bounds) const
{
    juce::Path path;

    const auto sampleRate = juce::jmax (audioProcessor.getSampleRate(), 44100.0);
    const auto nyquist = static_cast<float> (sampleRate * 0.5);
    const auto minFrequency = 20.0f;
    const auto maxFrequency = juce::jmax (minFrequency + 1.0f, juce::jmin (20000.0f, nyquist));
    const auto minDecibels = -72.0f;
    const auto maxDecibels = -12.0f;

    auto mapX = [bounds, minFrequency, maxFrequency] (float frequency)
    {
        const auto proportion = (std::log10 (frequency) - std::log10 (minFrequency))
                              / (std::log10 (maxFrequency) - std::log10 (minFrequency));
        return juce::jmap (proportion, 0.0f, 1.0f, bounds.getX(), bounds.getRight());
    };

    bool hasStarted = false;

    for (int bin = 1; bin < BolbolRefMasterAudioProcessor::spectrumBinCount; ++bin)
    {
        const auto frequency = static_cast<float> (bin) * static_cast<float> (sampleRate / BolbolRefMasterAudioProcessor::fftSize);

        if (frequency < minFrequency || frequency > maxFrequency)
            continue;

        const auto magnitude = juce::jmax (displaySpectrum[static_cast<size_t> (bin)], 1.0e-5f);
        const auto decibels = juce::Decibels::gainToDecibels (magnitude);
        const auto y = juce::jmap (juce::jlimit (minDecibels, maxDecibels, decibels),
                                   minDecibels, maxDecibels,
                                   bounds.getBottom(), bounds.getY());
        const auto x = mapX (frequency);

        if (! hasStarted)
        {
            path.startNewSubPath (x, y);
            hasStarted = true;
        }
        else
        {
            path.lineTo (x, y);
        }
    }

    if (! hasStarted)
        path.startNewSubPath (bounds.getX(), bounds.getBottom());

    return path;
}

void BolbolRefMasterAudioProcessorEditor::drawSpectrumScale (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    constexpr std::array<float, 5> frequencies { 20.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f };
    constexpr std::array<const char*, 5> frequencyLabels { "20", "100", "1k", "10k", "20k" };
    constexpr std::array<float, 4> decibelMarks { -12.0f, -24.0f, -36.0f, -48.0f };

    const auto sampleRate = juce::jmax (audioProcessor.getSampleRate(), 44100.0);
    const auto nyquist = static_cast<float> (sampleRate * 0.5);
    const auto minFrequency = 20.0f;
    const auto maxFrequency = juce::jmax (minFrequency + 1.0f, juce::jmin (20000.0f, nyquist));
    const auto minDecibels = -72.0f;
    const auto maxDecibels = -12.0f;

    auto mapX = [bounds, minFrequency, maxFrequency] (float frequency)
    {
        const auto proportion = (std::log10 (frequency) - std::log10 (minFrequency))
                              / (std::log10 (maxFrequency) - std::log10 (minFrequency));
        return juce::jmap (proportion, 0.0f, 1.0f, bounds.getX(), bounds.getRight());
    };

    g.setFont (juce::FontOptions (13.0f));

    for (auto decibels : decibelMarks)
    {
        const auto y = juce::jmap (decibels, minDecibels, maxDecibels, bounds.getBottom(), bounds.getY());
        g.setColour (scaleLineColour);
        g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());

        auto labelBounds = juce::Rectangle<float> (bounds.getX() + 6.0f, y - 8.0f, 42.0f, 16.0f).toNearestInt();
        g.setColour (labelColour);
        g.drawText (juce::String (juce::roundToInt (decibels)) + " dB", labelBounds, juce::Justification::centredLeft);
    }

    for (size_t i = 0; i < frequencies.size(); ++i)
    {
        const auto frequency = juce::jlimit (minFrequency, maxFrequency, frequencies[i]);
        const auto x = mapX (frequency);

        g.setColour (scaleLineColour);
        g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());

        auto labelBounds = juce::Rectangle<float> (x - 18.0f, bounds.getBottom() - 18.0f, 36.0f, 14.0f).toNearestInt();
        g.setColour (labelColour);
        g.drawText (frequencyLabels[i], labelBounds, juce::Justification::centred);
    }
}
