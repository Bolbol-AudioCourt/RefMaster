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
const auto successColour = juce::Colour (0xff5cb47a);
const auto warningColour = juce::Colour (0xffdfb85f);
const auto negativeColour = juce::Colour (0xffe58b8b);
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
bool BolbolRefMasterAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.isEmpty())
        return false;

    const juce::StringArray allowedExtensions { ".wav", ".aif", ".aiff", ".mp3", ".flac" };
    const auto file = juce::File (files[0]);

    return allowedExtensions.contains (file.getFileExtension().toLowerCase());
}

void BolbolRefMasterAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused (x, y);

    if (! isInterestedInFileDrag (files))
        return;

    audioProcessor.loadReferenceFile (juce::File (files[0]));
    repaint();
}

//==============================================================================
void BolbolRefMasterAudioProcessorEditor::mouseUp (const juce::MouseEvent& event)
{
    if (clearReferenceButtonBounds.contains (event.getPosition()))
    {
        audioProcessor.clearReferenceTrack();
        displayReferenceSpectrum.fill (0.0f);
        displayTargetPreviewSpectrum.fill (0.0f);
        repaint();
        return;
    }

    if (! referenceCardBounds.contains (event.getPosition()))
        return;

    referenceFileChooser.reset (new juce::FileChooser ("Select a reference track",
                                                        {},
                                                        "*.wav;*.aif;*.aiff;*.mp3;*.flac"));

    auto chooserFlags = juce::FileBrowserComponent::openMode
                      | juce::FileBrowserComponent::canSelectFiles;

    referenceFileChooser->launchAsync (chooserFlags,
                                       [this] (const juce::FileChooser& chooser)
                                       {
                                           const auto selectedFile = chooser.getResult();

                                           if (selectedFile == juce::File())
                                               return;

                                           audioProcessor.loadReferenceFile (selectedFile);
                                           repaint();
                                       });
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

    auto headerButtons = header.removeFromRight (240).reduced (0, 10);
    auto versionBounds = headerButtons.removeFromRight (54);
    auto bypassBounds = headerButtons.removeFromRight (92).reduced (8, 0);
    auto activeBounds = headerButtons.removeFromRight (92).reduced (8, 0);

    g.setColour (successColour.withAlpha (0.22f));
    g.fillRoundedRectangle (activeBounds.toFloat(), 10.0f);
    g.setColour (successColour);
    g.drawRoundedRectangle (activeBounds.toFloat(), 10.0f, 1.0f);
    g.drawText ("ACTIVE", activeBounds, juce::Justification::centred);

    g.setColour (juce::Colour (0x22ffffff));
    g.fillRoundedRectangle (bypassBounds.toFloat(), 10.0f);
    g.setColour (mutedTextColour);
    g.drawRoundedRectangle (bypassBounds.toFloat(), 10.0f, 1.0f);
    g.drawText ("BYPASS", bypassBounds, juce::Justification::centred);

    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("v0.1", versionBounds, juce::Justification::centredRight);

    auto content = bounds.reduced (20, 18);
    auto sidebar = content.removeFromRight (260);
    auto analyzerArea = content;
    auto summaryArea = analyzerArea.removeFromBottom (232);

    drawSpectrumAnalyzer (g, analyzerArea);
    drawBandSummary (g, summaryArea.reduced (0, 10));

    g.setColour (panelColour);
    g.fillRoundedRectangle (sidebar.toFloat(), 14.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (sidebar.toFloat(), 14.0f, 1.0f);

    auto sidebarContent = sidebar.reduced (18);

    auto referenceCard = sidebarContent.removeFromTop (146);
    referenceCardBounds = referenceCard;
    g.setColour (juce::Colour (0xff15161c));
    g.fillRoundedRectangle (referenceCard.toFloat(), 12.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (referenceCard.toFloat(), 12.0f, 1.0f);

    auto referenceText = referenceCard.reduced (14);
    const auto hasReference = audioProcessor.hasReferenceTrack();
    const auto referenceName = hasReference ? audioProcessor.getReferenceTrackName()
                                            : juce::String ("Click to load reference");
    const auto referenceInfo = hasReference ? audioProcessor.getReferenceTrackInfo()
                                            : juce::String ("WAV, AIFF, MP3, or FLAC");
    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("REFERENCE TRACK", referenceText.removeFromTop (22), juce::Justification::centredLeft);

    auto plusArea = referenceText.removeFromTop (34);
    g.setColour (juce::Colour (0x26ffffff));
    g.fillEllipse (plusArea.removeFromLeft (28).toFloat());
    g.setColour (mutedTextColour);
    g.drawText ("+", plusArea.withWidth (28), juce::Justification::centredLeft);

    auto clearRow = referenceText.removeFromBottom (22);
    clearReferenceButtonBounds = clearRow.removeFromRight (84);

    g.setColour (textColour);
    g.setFont (juce::FontOptions (17.0f));
    g.drawFittedText (referenceName, referenceText.removeFromTop (26), juce::Justification::centredLeft, 1);
    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (14.0f));
    g.drawFittedText (referenceInfo, referenceText.removeFromTop (20), juce::Justification::centredLeft, 1);

    if (hasReference)
    {
        g.setColour (juce::Colour (0x18ffffff));
        g.fillRoundedRectangle (clearReferenceButtonBounds.toFloat(), 8.0f);
        g.setColour (mutedTextColour);
        g.drawRoundedRectangle (clearReferenceButtonBounds.toFloat(), 8.0f, 1.0f);
        g.drawText ("RESET", clearReferenceButtonBounds, juce::Justification::centred);
    }
    else
    {
        clearReferenceButtonBounds = {};
    }

    sidebarContent.removeFromTop (12);

    auto modeTabs = sidebarContent.removeFromTop (34);
    auto simpleTab = modeTabs.removeFromLeft (modeTabs.getWidth() / 2);
    auto detailedTab = modeTabs;
    g.setColour (accentColour.withAlpha (0.18f));
    g.fillRoundedRectangle (simpleTab.toFloat(), 8.0f);
    g.setColour (accentColour);
    g.drawText ("Simple", simpleTab, juce::Justification::centred);
    g.setColour (juce::Colour (0x18ffffff));
    g.fillRoundedRectangle (detailedTab.toFloat(), 8.0f);
    g.setColour (mutedTextColour);
    g.drawText ("Detailed", detailedTab, juce::Justification::centred);

    sidebarContent.removeFromTop (14);

    auto blendPanel = sidebarContent.removeFromTop (236);
    g.setColour (juce::Colour (0xff15161c));
    g.fillRoundedRectangle (blendPanel.toFloat(), 12.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (blendPanel.toFloat(), 12.0f, 1.0f);

    auto blendContent = blendPanel.reduced (14);
    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("BLEND PER DIMENSION", blendContent.removeFromTop (24), juce::Justification::centredLeft);

    const std::array<std::pair<const char*, int>, 4> blendRows {{
        { "EQ", 62 },
        { "Dynamics", 40 },
        { "Width", 50 },
        { "Loudness", 80 },
    }};

    g.setFont (juce::FontOptions (15.0f));

    for (const auto& [label, value] : blendRows)
    {
        auto row = blendContent.removeFromTop (46);
        auto knobBounds = row.removeFromRight (52).reduced (4);
        auto valueBounds = row.removeFromRight (42);

        g.setColour (textColour);
        g.drawText (label, row, juce::Justification::centredLeft);

        g.setColour (juce::Colour (0x22ffffff));
        g.drawEllipse (knobBounds.toFloat(), 2.0f);

        juce::Path arc;
        const auto startAngle = juce::degreesToRadians (135.0f);
        const auto endAngle = juce::degreesToRadians (405.0f);
        const auto angle = juce::jmap (static_cast<float> (value), 0.0f, 100.0f, startAngle, endAngle);
        arc.addCentredArc (knobBounds.getCentreX(), knobBounds.getCentreY(),
                           knobBounds.getWidth() * 0.5f, knobBounds.getHeight() * 0.5f,
                           0.0f, startAngle, angle, true);
        g.setColour (accentColour);
        g.strokePath (arc, juce::PathStrokeType (2.0f));

        g.setColour (mutedTextColour);
        g.drawText (juce::String (value) + "%", valueBounds, juce::Justification::centredRight);
    }

    auto statusArea = sidebar.removeFromBottom (92).reduced (18, 14);
    g.setColour (juce::Colour (0xff111217));
    g.fillRoundedRectangle (statusArea.toFloat(), 12.0f);
    g.setColour (mutedTextColour);
    g.drawRoundedRectangle (statusArea.toFloat(), 12.0f, 1.0f);
    g.drawText ("Next", statusArea.removeFromTop (24), juce::Justification::centredLeft);

    g.setColour (textColour);
    g.drawFittedText ("Reference import, smoothing, and EQ matching come after the analyzer is stable.", statusArea, juce::Justification::topLeft, 3);

    auto footer = getLocalBounds().removeFromBottom (34).reduced (20, 4);
    g.setColour (juce::Colour (0xff121318));
    g.fillRoundedRectangle (footer.toFloat(), 10.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (footer.toFloat(), 10.0f, 1.0f);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (mutedTextColour);
    g.drawText ("TARGET", footer.removeFromLeft (58), juce::Justification::centredLeft);
    g.setColour (textColour);
    g.drawText ("-9.2 LUFS", footer.removeFromLeft (92), juce::Justification::centredLeft);
    g.setColour (mutedTextColour);
    g.drawText ("FFT", footer.removeFromLeft (36), juce::Justification::centredLeft);
    g.setColour (textColour);
    g.drawText ("2048", footer.removeFromLeft (56), juce::Justification::centredLeft);
    g.setColour (mutedTextColour);
    g.drawText ("MODE", footer.removeFromLeft (48), juce::Justification::centredLeft);
    g.setColour (warningColour);
    g.drawText ("ANALYZER", footer.removeFromLeft (90), juce::Justification::centredLeft);
    g.setColour (mutedTextColour);
    g.drawText ("CORR", footer.removeFromLeft (42), juce::Justification::centredLeft);
    g.setColour (successColour);
    g.drawText ("0.94", footer, juce::Justification::centredLeft);
}

void BolbolRefMasterAudioProcessorEditor::resized()
{
}

void BolbolRefMasterAudioProcessorEditor::timerCallback()
{
    const auto latestSpectrum = audioProcessor.getLatestMagnitudeSpectrum();
    const auto latestReferenceSpectrum = audioProcessor.getReferenceMagnitudeSpectrum();
    const auto hasReference = audioProcessor.hasReferenceTrack();

    for (size_t i = 0; i < displaySpectrum.size(); ++i)
    {
        displaySpectrum[i] = juce::jmax (latestSpectrum[i], displaySpectrum[i] * 0.82f);
        displayReferenceSpectrum[i] = juce::jmax (latestReferenceSpectrum[i], displayReferenceSpectrum[i] * 0.9f);

        if (hasReference)
        {
            const auto inputDb = juce::Decibels::gainToDecibels (juce::jmax (displaySpectrum[i], 1.0e-5f));
            const auto referenceDb = juce::Decibels::gainToDecibels (juce::jmax (displayReferenceSpectrum[i], 1.0e-5f));
            const auto targetDb = inputDb + ((referenceDb - inputDb) * 0.5f);
            const auto targetMagnitude = juce::Decibels::decibelsToGain (targetDb);
            displayTargetPreviewSpectrum[i] = juce::jmax (targetMagnitude, displayTargetPreviewSpectrum[i] * 0.88f);
        }
        else
        {
            displayTargetPreviewSpectrum[i] *= 0.85f;
        }
    }

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

    auto tabsArea = content.removeFromBottom (34);
    auto legendArea = content.removeFromBottom (28);
    auto graphBounds = content.toFloat().reduced (0.0f, 12.0f);
    auto plotBounds = graphBounds.reduced (14.0f, 12.0f);

    g.setColour (graphColour);
    g.fillRoundedRectangle (graphBounds, 12.0f);

    drawSpectrumScale (g, plotBounds);

    auto spectrumPath = createSpectrumPath (plotBounds, displaySpectrum);

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

    if (audioProcessor.hasReferenceTrack())
    {
        auto referencePath = createSpectrumPath (plotBounds, displayReferenceSpectrum);
        g.setColour (legendColour);
        g.strokePath (referencePath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto targetPreviewPath = createSpectrumPath (plotBounds, displayTargetPreviewSpectrum);
        g.setColour (warningColour);
        g.strokePath (targetPreviewPath, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    g.setColour (accentColour);
    g.strokePath (spectrumPath, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    drawLegend (g, legendArea);

    auto activeTab = tabsArea.removeFromLeft (104).reduced (0, 2);
    auto secondaryTab = tabsArea.removeFromLeft (104).reduced (8, 2);
    auto thirdTab = tabsArea.removeFromLeft (132).reduced (8, 2);

    g.setColour (accentColour.withAlpha (0.16f));
    g.fillRoundedRectangle (activeTab.toFloat(), 8.0f);
    g.setColour (accentColour);
    g.drawRoundedRectangle (activeTab.toFloat(), 8.0f, 1.0f);
    g.drawText ("EQ match", activeTab, juce::Justification::centred);

    g.setColour (juce::Colour (0x18ffffff));
    g.fillRoundedRectangle (secondaryTab.toFloat(), 8.0f);
    g.fillRoundedRectangle (thirdTab.toFloat(), 8.0f);
    g.setColour (mutedTextColour);
    g.drawText ("Dynamics", secondaryTab, juce::Justification::centred);
    g.drawText ("Stereo width", thirdTab, juce::Justification::centred);
}

void BolbolRefMasterAudioProcessorEditor::drawBandSummary (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    g.setColour (panelColour);
    g.fillRoundedRectangle (bounds.toFloat(), 16.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (bounds.toFloat(), 16.0f, 1.0f);

    auto content = bounds.reduced (20, 16);
    auto header = content.removeFromTop (24);

    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("COMPARISON SUMMARY", header, juce::Justification::centredLeft);

    struct BandRow
    {
        const char* label;
        float lowHz;
        float highHz;
        juce::Colour colour;
    };

    const std::array<BandRow, 5> bands {{
        { "Sub bass (20-80)", 20.0f, 80.0f, accentColour },
        { "Bass (80-250)", 80.0f, 250.0f, legendColour },
        { "Mids (250-2k)", 250.0f, 2000.0f, successColour },
        { "Hi mids (2k-8k)", 2000.0f, 8000.0f, warningColour },
        { "Air (8k-20k)", 8000.0f, 20000.0f, negativeColour },
    }};

    const auto sampleRate = juce::jmax (audioProcessor.getSampleRate(), 44100.0);
    const auto binWidth = static_cast<float> (sampleRate / BolbolRefMasterAudioProcessor::fftSize);
    const auto hasReference = audioProcessor.hasReferenceTrack();
    const auto inputNormalisationDb = calculateSpectrumNormalisationDb (displaySpectrum, sampleRate);
    const auto referenceNormalisationDb = calculateSpectrumNormalisationDb (displayReferenceSpectrum, sampleRate);

    g.setFont (juce::FontOptions (15.0f));

    for (const auto& band : bands)
    {
        auto row = content.removeFromTop (34);
        auto labelBounds = row.removeFromLeft (220);
        auto valueBounds = row.removeFromRight (74);
        auto verdictBounds = row.removeFromRight (84);
        auto meterBounds = row.reduced (6, 8);

        g.setColour (textColour);
        g.drawText (band.label, labelBounds, juce::Justification::centredLeft);

        g.setColour (juce::Colour (0x1effffff));
        g.fillRoundedRectangle (meterBounds.toFloat(), 3.0f);

        float averageDeltaDb = 0.0f;

        if (hasReference)
        {
            int count = 0;

            for (int bin = 1; bin < BolbolRefMasterAudioProcessor::spectrumBinCount; ++bin)
            {
                const auto frequency = static_cast<float> (bin) * binWidth;

                if (frequency < band.lowHz || frequency >= band.highHz)
                    continue;

                const auto inputDb = getNormalisedSpectrumDb (displaySpectrum, bin, inputNormalisationDb);
                const auto referenceDb = getNormalisedSpectrumDb (displayReferenceSpectrum, bin, referenceNormalisationDb);
                averageDeltaDb += (referenceDb - inputDb);
                ++count;
            }

            if (count > 0)
                averageDeltaDb /= static_cast<float> (count);
        }

        const auto clampedDelta = juce::jlimit (-6.0f, 6.0f, averageDeltaDb);
        const auto meterFillWidth = juce::jmap (std::abs (clampedDelta), 0.0f, 6.0f, 0.0f, static_cast<float> (meterBounds.getWidth()));
        auto meterFill = meterBounds;
        meterFill.setWidth (juce::roundToInt (meterFillWidth));

        g.setColour (band.colour.withAlpha (0.9f));
        g.fillRoundedRectangle (meterFill.toFloat(), 3.0f);

        const auto verdict = ! hasReference ? "waiting"
                            : (clampedDelta > 1.0f ? "boost"
                            : (clampedDelta < -1.0f ? "cut" : "close"));

        const auto verdictColour = ! hasReference ? mutedTextColour
                                  : (juce::String (verdict) == "close" ? successColour
                                  : (juce::String (verdict) == "boost" ? warningColour : negativeColour));

        g.setColour (hasReference ? textColour : mutedTextColour);
        g.drawText ((hasReference ? juce::String (clampedDelta, 1) : juce::String ("--")) + " dB",
                    valueBounds, juce::Justification::centredRight);

        g.setColour (verdictColour);
        g.drawText (verdict, verdictBounds, juce::Justification::centredRight);
    }
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

    auto secondaryBounds = bounds.removeFromLeft (200);
    auto secondaryLine = secondaryBounds.removeFromLeft (28).toFloat().withTrimmedTop (12.0f).withHeight (3.0f);
    g.setColour (legendColour);
    g.fillRoundedRectangle (secondaryLine, 1.5f);
    g.setColour (mutedTextColour);
    g.drawText ("Reference", secondaryBounds, juce::Justification::centredLeft);

    auto tertiaryBounds = bounds.removeFromLeft (200);
    auto tertiaryLine = tertiaryBounds.removeFromLeft (28).toFloat().withTrimmedTop (12.0f).withHeight (3.0f);
    g.setColour (warningColour);
    g.fillRoundedRectangle (tertiaryLine, 1.5f);
    g.setColour (mutedTextColour);
    g.drawText ("Target preview", tertiaryBounds, juce::Justification::centredLeft);
}

juce::Path BolbolRefMasterAudioProcessorEditor::createSpectrumPath (
    juce::Rectangle<float> bounds,
    const std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount>& spectrum) const
{
    juce::Path path;

    const auto sampleRate = juce::jmax (audioProcessor.getSampleRate(), 44100.0);
    const auto nyquist = static_cast<float> (sampleRate * 0.5);
    const auto minFrequency = 20.0f;
    const auto maxFrequency = juce::jmax (minFrequency + 1.0f, juce::jmin (20000.0f, nyquist));
    const auto minDecibels = -72.0f;
    const auto maxDecibels = -12.0f;
    const auto normalisationDb = calculateSpectrumNormalisationDb (spectrum, sampleRate);

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

        const auto decibels = getNormalisedSpectrumDb (spectrum, bin, normalisationDb);
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

float BolbolRefMasterAudioProcessorEditor::calculateSpectrumNormalisationDb (
    const std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount>& spectrum,
    double sampleRate) const
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

float BolbolRefMasterAudioProcessorEditor::getNormalisedSpectrumDb (
    const std::array<float, BolbolRefMasterAudioProcessor::spectrumBinCount>& spectrum,
    int bin,
    float normalisationDb) const
{
    const auto magnitude = juce::jmax (spectrum[static_cast<size_t> (bin)], 1.0e-5f);
    return juce::Decibels::gainToDecibels (magnitude) - normalisationDb - 24.0f;
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
