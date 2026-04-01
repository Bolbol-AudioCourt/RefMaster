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
    previewEqToggle.setButtonText ("Preview EQ");
    previewEqToggle.setToggleState (audioProcessor.isPreviewEqEnabled(), juce::dontSendNotification);
    previewEqToggle.onClick = [this]
    {
        audioProcessor.setPreviewEqEnabled (previewEqToggle.getToggleState());
        repaint();
    };
    addAndMakeVisible (previewEqToggle);

    previewBypassToggle.setButtonText ("Bypass");
    previewBypassToggle.setToggleState (audioProcessor.isPreviewEqBypassed(), juce::dontSendNotification);
    previewBypassToggle.onClick = [this]
    {
        audioProcessor.setPreviewEqBypassed (previewBypassToggle.getToggleState());
        repaint();
    };
    addAndMakeVisible (previewBypassToggle);

    previewBlendSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    previewBlendSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    previewBlendSlider.setRange (0.0, 100.0, 1.0);
    previewBlendSlider.setValue (audioProcessor.getPreviewBlendAmount() * 100.0f, juce::dontSendNotification);
    previewBlendSlider.setColour (juce::Slider::trackColourId, accentColour);
    previewBlendSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0x22ffffff));
    previewBlendSlider.setColour (juce::Slider::thumbColourId, textColour);
    previewBlendSlider.onValueChange = [this]
    {
        audioProcessor.setPreviewBlendAmount (static_cast<float> (previewBlendSlider.getValue() / 100.0));
        repaint();
    };
    addAndMakeVisible (previewBlendSlider);

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
    if (applyMatchButtonBounds.contains (event.getPosition()))
    {
        if (audioProcessor.hasReferenceTrack())
        {
            audioProcessor.setPreviewEqEnabled (true);
            audioProcessor.setPreviewEqBypassed (false);
            previewEqToggle.setToggleState (true, juce::dontSendNotification);
            previewBypassToggle.setToggleState (false, juce::dontSendNotification);
            repaint();
        }

        return;
    }

    if (resetAllButtonBounds.contains (event.getPosition()))
    {
        audioProcessor.clearReferenceTrack();
        audioProcessor.setPreviewBlendAmount (0.5f);
        previewEqToggle.setToggleState (false, juce::dontSendNotification);
        previewBypassToggle.setToggleState (false, juce::dontSendNotification);
        previewBlendSlider.setValue (50.0, juce::dontSendNotification);
        displayReferenceSpectrum.fill (0.0f);
        displayTargetPreviewSpectrum.fill (0.0f);
        repaint();
        return;
    }

    if (simpleTabBounds.contains (event.getPosition()))
    {
        showDetailedComparison = false;
        repaint();
        return;
    }

    if (detailedTabBounds.contains (event.getPosition()))
    {
        showDetailedComparison = true;
        repaint();
        return;
    }

    if (clearReferenceButtonBounds.contains (event.getPosition()))
    {
        audioProcessor.clearReferenceTrack();
        previewEqToggle.setToggleState (false, juce::dontSendNotification);
        previewBypassToggle.setToggleState (false, juce::dontSendNotification);
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
    g.drawText (audioProcessor.isPreviewEqBypassed() ? "PREVIEW OFF" : "BYPASS",
                bypassBounds,
                juce::Justification::centred);

    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("v0.1", versionBounds, juce::Justification::centredRight);

    auto content = bounds.reduced (20, 18);
    auto sidebar = content.removeFromRight (260);
    auto analyzerArea = content;
    auto summaryArea = analyzerArea.removeFromBottom (232);

    drawSpectrumAnalyzer (g, analyzerArea);

    if (showDetailedComparison)
        drawDetailedSummary (g, summaryArea.reduced (0, 10));
    else
        drawBandSummary (g, summaryArea.reduced (0, 10));

    g.setColour (panelColour);
    g.fillRoundedRectangle (sidebar.toFloat(), 14.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (sidebar.toFloat(), 14.0f, 1.0f);

    auto sidebarContent = sidebar.reduced (18);

    auto referenceCard = sidebarContent.removeFromTop (146);
    referenceCardBounds = referenceCard;
    previewEqToggle.setEnabled (audioProcessor.hasReferenceTrack());
    previewBypassToggle.setEnabled (audioProcessor.hasReferenceTrack() && audioProcessor.isPreviewEqEnabled());
    g.setColour (juce::Colour (0xff15161c));
    g.fillRoundedRectangle (referenceCard.toFloat(), 12.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (referenceCard.toFloat(), 12.0f, 1.0f);

    auto referenceText = referenceCard.reduced (14);
    const auto hasReference = audioProcessor.hasReferenceTrack();
    const auto correlation = hasReference ? calculateSpectrumCorrelation() : 0.0f;
    const auto previewTrimDb = hasReference ? audioProcessor.getPreviewOutputTrimDb() : 0.0f;
    const auto previewProcessingActive = audioProcessor.isPreviewEqActive();
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
    g.setColour (hasReference ? successColour : mutedTextColour);
    g.drawText (hasReference ? juce::String (juce::CharPointer_UTF8 ("\xE2\x9C\x93")) : "+",
                plusArea.withWidth (28),
                juce::Justification::centredLeft);

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
    simpleTabBounds = simpleTab;
    detailedTabBounds = detailedTab;

    g.setColour (! showDetailedComparison ? accentColour.withAlpha (0.18f) : juce::Colour (0x18ffffff));
    g.fillRoundedRectangle (simpleTab.toFloat(), 8.0f);
    g.setColour (! showDetailedComparison ? accentColour : mutedTextColour);
    g.drawText ("Simple", simpleTab, juce::Justification::centred);

    g.setColour (showDetailedComparison ? accentColour.withAlpha (0.18f) : juce::Colour (0x18ffffff));
    g.fillRoundedRectangle (detailedTab.toFloat(), 8.0f);
    g.setColour (showDetailedComparison ? accentColour : mutedTextColour);
    g.drawText ("Detailed", detailedTab, juce::Justification::centred);

    sidebarContent.removeFromTop (14);

    auto toggleArea = sidebarContent.removeFromTop (26);
    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Audio preview", toggleArea.removeFromLeft (96), juce::Justification::centredLeft);

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
        { "EQ", juce::roundToInt (previewBlendSlider.getValue()) },
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

        g.setColour ((juce::String (label) == "EQ" && ! hasReference) ? mutedTextColour : textColour);
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
        g.setColour ((juce::String (label) == "EQ" && ! hasReference) ? mutedTextColour : accentColour);
        g.strokePath (arc, juce::PathStrokeType (2.0f));

        g.setColour (mutedTextColour);
        g.drawText (juce::String (value) + "%", valueBounds, juce::Justification::centredRight);
    }

    auto sliderLabelBounds = blendPanel.removeFromBottom (32).reduced (14, 6);
    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Preview mix", sliderLabelBounds.removeFromLeft (90), juce::Justification::centredLeft);
    g.setColour (textColour);
    g.drawText (juce::String (juce::roundToInt (previewBlendSlider.getValue())) + "%",
                sliderLabelBounds,
                juce::Justification::centredRight);

    auto actionArea = sidebar.removeFromBottom (124).reduced (18, 14);
    auto applyButton = actionArea.removeFromTop (32);
    auto resetButton = actionArea.removeFromTop (32);
    actionArea.removeFromTop (8);
    auto statusArea = actionArea;

    applyMatchButtonBounds = applyButton;
    resetAllButtonBounds = resetButton;

    g.setColour (hasReference ? accentColour.withAlpha (0.18f) : juce::Colour (0x14ffffff));
    g.fillRoundedRectangle (applyButton.toFloat(), 10.0f);
    g.setColour (hasReference ? accentColour : mutedTextColour);
    g.drawRoundedRectangle (applyButton.toFloat(), 10.0f, 1.0f);
    g.drawText ("APPLY MATCH", applyButton, juce::Justification::centred);

    g.setColour (juce::Colour (0x14ffffff));
    g.fillRoundedRectangle (resetButton.toFloat(), 10.0f);
    g.setColour (mutedTextColour);
    g.drawRoundedRectangle (resetButton.toFloat(), 10.0f, 1.0f);
    g.drawText ("RESET ALL", resetButton, juce::Justification::centred);

    g.setColour (juce::Colour (0xff111217));
    g.fillRoundedRectangle (statusArea.toFloat(), 12.0f);
    g.setColour (mutedTextColour);
    g.drawRoundedRectangle (statusArea.toFloat(), 12.0f, 1.0f);
    g.drawText ("Next", statusArea.removeFromTop (24), juce::Justification::centredLeft);

    g.setColour (textColour);
    g.drawFittedText (hasReference
                          ? (previewProcessingActive
                                 ? "Reference loaded. Preview EQ is active and the analyzer is showing processor-backed matching guidance."
                                 : "Reference loaded. Comparison is active. Enable Preview EQ to audition the current matching guidance.")
                          : "Load or drop a reference track to unlock comparison, target preview, and Preview EQ controls.",
                      statusArea,
                      juce::Justification::topLeft,
                      3);

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
    g.setColour (previewProcessingActive ? warningColour : mutedTextColour);
    g.drawText (previewProcessingActive ? "PREVIEW EQ" : "ANALYZER", footer.removeFromLeft (90), juce::Justification::centredLeft);
    g.setColour (mutedTextColour);
    g.drawText ("CORR", footer.removeFromLeft (42), juce::Justification::centredLeft);
    g.setColour (successColour);
    g.drawText (audioProcessor.hasReferenceTrack() ? juce::String (correlation, 2) : juce::String ("--"),
                footer,
                juce::Justification::centredLeft);
    g.setColour (mutedTextColour);
    g.drawText ("TRIM", footer.removeFromLeft (42), juce::Justification::centredLeft);
    g.setColour (previewProcessingActive ? textColour : mutedTextColour);
    g.drawText (hasReference ? juce::String (previewTrimDb, 1) + " dB" : juce::String ("--"),
                footer,
                juce::Justification::centredLeft);
}

void BolbolRefMasterAudioProcessorEditor::resized()
{
    auto content = getLocalBounds().reduced (20, 18);
    auto sidebar = content.removeFromRight (260);
    auto sidebarContent = sidebar.reduced (18);

    sidebarContent.removeFromTop (146);
    sidebarContent.removeFromTop (12);
    sidebarContent.removeFromTop (34);
    sidebarContent.removeFromTop (14);
    auto toggleArea = sidebarContent.removeFromTop (26);
    auto bypassBounds = toggleArea.removeFromRight (90);
    previewBypassToggle.setBounds (bypassBounds);
    previewEqToggle.setBounds (toggleArea.removeFromRight (120));

    auto blendPanel = sidebarContent.removeFromTop (236);
    auto sliderBounds = blendPanel.removeFromBottom (32).reduced (14, 6);
    sliderBounds.removeFromLeft (92);
    previewBlendSlider.setBounds (sliderBounds.withHeight (16).withY (sliderBounds.getY() + 8));
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
            const auto targetDb = inputDb + ((referenceDb - inputDb) * audioProcessor.getPreviewBlendAmount());
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
        drawPreviewMatchPoints (g, plotBounds);
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

    const auto hasReference = audioProcessor.hasReferenceTrack();
    const auto previewBandAdjustments = audioProcessor.getPreviewBandAdjustmentsDb();
    const auto previewMatchPoints = audioProcessor.getPreviewMatchPoints();
    const auto currentPreviewBandGainsDb = audioProcessor.getCurrentPreviewEqBandGainsDb();

    g.setFont (juce::FontOptions (15.0f));

    for (const auto& band : bands)
    {
        auto row = content.removeFromTop (34);
        auto labelBounds = row.removeFromLeft (220);
        auto valueBounds = row.removeFromRight (74);
        auto verdictBounds = row.removeFromRight (84);
        auto frequencyBounds = row.removeFromRight (74);
        auto meterBounds = row.reduced (6, 8);

        g.setColour (textColour);
        g.drawText (band.label, labelBounds, juce::Justification::centredLeft);

        g.setColour (juce::Colour (0x1effffff));
        g.fillRoundedRectangle (meterBounds.toFloat(), 3.0f);

        const auto bandIndex = static_cast<size_t> (&band - bands.data());
        const auto clampedDelta = previewBandAdjustments[bandIndex];
        const auto currentPreviewGain = currentPreviewBandGainsDb[bandIndex];
        const auto meterFillWidth = juce::jmap (std::abs (currentPreviewGain), 0.0f, 6.0f, 0.0f, static_cast<float> (meterBounds.getWidth()));
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
        g.drawText ((hasReference ? juce::String (currentPreviewGain, 1) : juce::String ("--")) + " dB",
                    valueBounds, juce::Justification::centredRight);

        const auto matchPoint = previewMatchPoints[bandIndex];
        const auto frequencyLabel = matchPoint.frequencyHz >= 1000.0f
                                      ? juce::String (matchPoint.frequencyHz / 1000.0f, 1) + "k"
                                      : juce::String (juce::roundToInt (matchPoint.frequencyHz));
        g.setColour (mutedTextColour);
        g.drawText (hasReference ? frequencyLabel : juce::String ("--"),
                    frequencyBounds,
                    juce::Justification::centredRight);

        g.setColour (verdictColour);
        g.drawText (verdict, verdictBounds, juce::Justification::centredRight);
    }
}

void BolbolRefMasterAudioProcessorEditor::drawDetailedSummary (juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    g.setColour (panelColour);
    g.fillRoundedRectangle (bounds.toFloat(), 16.0f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (bounds.toFloat(), 16.0f, 1.0f);

    auto content = bounds.reduced (20, 16);
    auto header = content.removeFromTop (24);

    g.setColour (mutedTextColour);
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("DETAILED PREVIEW POINTS", header, juce::Justification::centredLeft);

    const auto hasReference = audioProcessor.hasReferenceTrack();
    const auto previewMatchPoints = audioProcessor.getPreviewMatchPoints();
    const auto currentPreviewBandGainsDb = audioProcessor.getCurrentPreviewEqBandGainsDb();

    g.setFont (juce::FontOptions (14.0f));
    g.setColour (labelColour);
    auto labels = content.removeFromTop (22);
    labels.removeFromLeft (20);
    g.drawText ("Frequency", labels.removeFromLeft (150), juce::Justification::centredLeft);
    g.drawText ("Gain", labels.removeFromLeft (80), juce::Justification::centredLeft);
    g.drawText ("Q", labels.removeFromLeft (60), juce::Justification::centredLeft);
    g.drawText ("Status", labels, juce::Justification::centredLeft);

    for (size_t index = 0; index < previewMatchPoints.size(); ++index)
    {
        const auto& matchPoint = previewMatchPoints[index];
        auto row = content.removeFromTop (30);
        auto dotBounds = row.removeFromLeft (14).reduced (2);
        auto frequencyBounds = row.removeFromLeft (156);
        auto gainBounds = row.removeFromLeft (80);
        auto qBounds = row.removeFromLeft (60);
        auto statusBounds = row;

        g.setColour (warningColour);
        g.fillEllipse (dotBounds.toFloat());

        const auto frequencyText = matchPoint.frequencyHz >= 1000.0f
                                     ? juce::String (matchPoint.frequencyHz / 1000.0f, 1) + " kHz"
                                     : juce::String (juce::roundToInt (matchPoint.frequencyHz)) + " Hz";
        const auto gainText = hasReference ? juce::String (currentPreviewBandGainsDb[index], 1) + " dB" : juce::String ("--");
        const auto qText = juce::String (matchPoint.q, 2);
        const auto statusText = ! hasReference ? "waiting"
                               : (currentPreviewBandGainsDb[index] > 1.0f ? "boost preview"
                               : (currentPreviewBandGainsDb[index] < -1.0f ? "cut preview" : "close"));
        const auto statusColour = ! hasReference ? mutedTextColour
                                 : (statusText == "close" ? successColour
                                 : (currentPreviewBandGainsDb[index] > 0.0f ? warningColour : negativeColour));

        g.setColour (textColour);
        g.drawText (frequencyText, frequencyBounds, juce::Justification::centredLeft);
        g.drawText (gainText, gainBounds, juce::Justification::centredLeft);
        g.drawText (qText, qBounds, juce::Justification::centredLeft);
        g.setColour (statusColour);
        g.drawText (statusText, statusBounds, juce::Justification::centredLeft);
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

float BolbolRefMasterAudioProcessorEditor::calculateSpectrumCorrelation() const
{
    if (! audioProcessor.hasReferenceTrack())
        return 0.0f;

    const auto sampleRate = juce::jmax (audioProcessor.getSampleRate(), 44100.0);
    const auto binWidth = static_cast<float> (sampleRate / BolbolRefMasterAudioProcessor::fftSize);
    const auto inputNormalisationDb = calculateSpectrumNormalisationDb (displaySpectrum, sampleRate);
    const auto referenceNormalisationDb = calculateSpectrumNormalisationDb (displayReferenceSpectrum, sampleRate);

    float dot = 0.0f;
    float inputEnergy = 0.0f;
    float referenceEnergy = 0.0f;

    for (int bin = 1; bin < BolbolRefMasterAudioProcessor::spectrumBinCount; ++bin)
    {
        const auto frequency = static_cast<float> (bin) * binWidth;

        if (frequency < 20.0f || frequency > 20000.0f)
            continue;

        const auto inputDb = getNormalisedSpectrumDb (displaySpectrum, bin, inputNormalisationDb);
        const auto referenceDb = getNormalisedSpectrumDb (displayReferenceSpectrum, bin, referenceNormalisationDb);

        dot += inputDb * referenceDb;
        inputEnergy += inputDb * inputDb;
        referenceEnergy += referenceDb * referenceDb;
    }

    if (inputEnergy <= 0.0f || referenceEnergy <= 0.0f)
        return 0.0f;

    const auto cosineSimilarity = dot / std::sqrt (inputEnergy * referenceEnergy);
    return juce::jlimit (0.0f, 1.0f, 0.5f * (cosineSimilarity + 1.0f));
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

void BolbolRefMasterAudioProcessorEditor::drawPreviewMatchPoints (juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    if (! audioProcessor.hasReferenceTrack())
        return;

    const auto sampleRate = juce::jmax (audioProcessor.getSampleRate(), 44100.0);
    const auto nyquist = static_cast<float> (sampleRate * 0.5);
    const auto minFrequency = 20.0f;
    const auto maxFrequency = juce::jmax (minFrequency + 1.0f, juce::jmin (20000.0f, nyquist));
    const auto minDecibels = -72.0f;
    const auto maxDecibels = -12.0f;
    const auto normalisationDb = calculateSpectrumNormalisationDb (displayTargetPreviewSpectrum, sampleRate);
    const auto previewMatchPoints = audioProcessor.getPreviewMatchPoints();

    auto mapX = [bounds, minFrequency, maxFrequency] (float frequency)
    {
        const auto proportion = (std::log10 (frequency) - std::log10 (minFrequency))
                              / (std::log10 (maxFrequency) - std::log10 (minFrequency));
        return juce::jmap (proportion, 0.0f, 1.0f, bounds.getX(), bounds.getRight());
    };

    g.setFont (juce::FontOptions (11.0f));

    for (const auto& matchPoint : previewMatchPoints)
    {
        const auto x = mapX (juce::jlimit (minFrequency, maxFrequency, matchPoint.frequencyHz));
        const auto decibels = juce::jlimit (minDecibels,
                                            maxDecibels,
                                            matchPoint.gainDb - normalisationDb - 24.0f);
        const auto y = juce::jmap (decibels, minDecibels, maxDecibels, bounds.getBottom(), bounds.getY());
        const auto markerBounds = juce::Rectangle<float> (x - 4.0f, y - 4.0f, 8.0f, 8.0f);

        g.setColour (warningColour);
        g.fillEllipse (markerBounds);
        g.setColour (backgroundColour);
        g.drawEllipse (markerBounds, 1.0f);

        auto labelBounds = juce::Rectangle<float> (x - 18.0f, y - 18.0f, 36.0f, 12.0f).toNearestInt();
        g.setColour (labelColour);
        const auto label = matchPoint.frequencyHz >= 1000.0f
                             ? juce::String (matchPoint.frequencyHz / 1000.0f, 1) + "k"
                             : juce::String (juce::roundToInt (matchPoint.frequencyHz));
        g.drawText (label, labelBounds, juce::Justification::centred);
    }
}
