#include "Source/PluginProcessor.h"

#include <cmath>
#include <iostream>
#include <memory>

struct RunStats
{
    double inputRms = 0.0;
    double outputRms = 0.0;
    double diffRms = 0.0;
    double maxAbsDiff = 0.0;
    bool anyNaN = false;
    bool anyInf = false;
};

static RunStats runPreparedProcessor (BolbolRefMasterAudioProcessor& processor,
                                      juce::AudioFormatReader& reader,
                                      int maxBlocks)
{
    const int numChannels = juce::jmin (2, static_cast<int> (reader.numChannels));
    juce::AudioBuffer<float> ioBuffer (juce::jmax (2, numChannels), 512);
    juce::AudioBuffer<float> dryCopy (juce::jmax (2, numChannels), 512);
    juce::MidiBuffer midi;

    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    double diffEnergy = 0.0;
    long long sampleCount = 0;
    double maxAbsDiff = 0.0;
    bool anyNaN = false;
    bool anyInf = false;
    int blocks = 0;

    for (juce::int64 start = 0; start < reader.lengthInSamples && blocks < maxBlocks; start += ioBuffer.getNumSamples(), ++blocks)
    {
        const int numToRead = static_cast<int> (juce::jmin<juce::int64> (ioBuffer.getNumSamples(), reader.lengthInSamples - start));
        ioBuffer.clear();
        dryCopy.clear();
        reader.read (&ioBuffer, 0, numToRead, start, true, true);
        dryCopy.makeCopyOf (ioBuffer, true);

        processor.processBlock (ioBuffer, midi);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const auto* in = dryCopy.getReadPointer (channel);
            const auto* out = ioBuffer.getReadPointer (channel);

            for (int sample = 0; sample < numToRead; ++sample)
            {
                const double inputSample = in[sample];
                const double outputSample = out[sample];
                const double diff = outputSample - inputSample;

                inputEnergy += inputSample * inputSample;
                outputEnergy += outputSample * outputSample;
                diffEnergy += diff * diff;
                maxAbsDiff = juce::jmax (maxAbsDiff, std::abs (diff));
                anyNaN = anyNaN || std::isnan (outputSample);
                anyInf = anyInf || std::isinf (outputSample);
                ++sampleCount;
            }
        }
    }

    processor.releaseResources();

    RunStats stats;
    stats.inputRms = std::sqrt (inputEnergy / juce::jmax<long long> (1, sampleCount));
    stats.outputRms = std::sqrt (outputEnergy / juce::jmax<long long> (1, sampleCount));
    stats.diffRms = std::sqrt (diffEnergy / juce::jmax<long long> (1, sampleCount));
    stats.maxAbsDiff = maxAbsDiff;
    stats.anyNaN = anyNaN;
    stats.anyInf = anyInf;
    return stats;
}

template <typename ConfigureFn>
static RunStats runProcessorOnFile (const juce::File& inputFile,
                                    ConfigureFn&& configureProcessor,
                                    int maxBlocks,
                                    bool loadReference)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (inputFile));
    if (reader == nullptr)
        throw std::runtime_error ("Failed to open source audio file");

    BolbolRefMasterAudioProcessor processor;
    processor.prepareToPlay (reader->sampleRate, 512);

    if (loadReference && ! processor.loadReferenceFile (inputFile))
        throw std::runtime_error ("Processor failed to load reference file");

    configureProcessor (processor);
    return runPreparedProcessor (processor, *reader, maxBlocks);
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cerr << "Usage: ProcessorSmokeMain <audio-file>\n";
        return 1;
    }

    const auto inputPath = juce::String::fromUTF8 (argv[1]);
    const juce::File inputFile = juce::File::isAbsolutePath (inputPath)
                                   ? juce::File (inputPath)
                                   : juce::File::getCurrentWorkingDirectory().getChildFile (inputPath);

    if (! inputFile.existsAsFile())
    {
        std::cerr << "Audio file not found: " << inputFile.getFullPathName() << "\n";
        return 1;
    }

    try
    {
        BolbolRefMasterAudioProcessor probe;
        const bool loaded = probe.loadReferenceFile (inputFile);
        std::cout << "reference_loaded=" << (loaded ? "true" : "false") << "\n";
        std::cout << "reference_name=" << probe.getReferenceTrackName() << "\n";
        std::cout << "reference_info=" << probe.getReferenceTrackInfo() << "\n";

        const auto dry = runProcessorOnFile (inputFile,
                                             [] (BolbolRefMasterAudioProcessor& processor)
                                             {
                                                 processor.setPreviewBlendAmount (0.5f);
                                                 processor.setPreviewEqEnabled (false);
                                                 processor.setPreviewEqBypassed (false);
                                             },
                                             256,
                                             true);
        const auto wet = runProcessorOnFile (inputFile,
                                             [] (BolbolRefMasterAudioProcessor& processor)
                                             {
                                                 processor.setPreviewBlendAmount (0.5f);
                                                 processor.setPreviewEqEnabled (true);
                                                 processor.setPreviewEqBypassed (false);
                                             },
                                             256,
                                             true);
        const auto bypassed = runProcessorOnFile (inputFile,
                                                  [] (BolbolRefMasterAudioProcessor& processor)
                                                  {
                                                      processor.setPreviewBlendAmount (0.5f);
                                                      processor.setPreviewEqEnabled (true);
                                                      processor.setPreviewEqBypassed (true);
                                                  },
                                                  256,
                                                  true);
        const auto zeroBlend = runProcessorOnFile (inputFile,
                                                   [] (BolbolRefMasterAudioProcessor& processor)
                                                   {
                                                       processor.setPreviewBlendAmount (0.0f);
                                                       processor.setPreviewEqEnabled (true);
                                                       processor.setPreviewEqBypassed (false);
                                                   },
                                                   256,
                                                   true);
        const auto applied = runProcessorOnFile (inputFile,
                                                 [] (BolbolRefMasterAudioProcessor& processor)
                                                 {
                                                     processor.setPreviewBlendAmount (1.0f);
                                                     processor.commitPreviewAsAppliedMatch();
                                                 },
                                                 256,
                                                 true);
        const auto appliedBypassed = runProcessorOnFile (inputFile,
                                                         [] (BolbolRefMasterAudioProcessor& processor)
                                                         {
                                                             processor.setPreviewBlendAmount (1.0f);
                                                             processor.commitPreviewAsAppliedMatch();
                                                             processor.setPreviewEqBypassed (true);
                                                         },
                                                         256,
                                                         true);
        const auto clearedApplied = runProcessorOnFile (inputFile,
                                                        [] (BolbolRefMasterAudioProcessor& processor)
                                                        {
                                                            processor.setPreviewBlendAmount (1.0f);
                                                            processor.commitPreviewAsAppliedMatch();
                                                            processor.clearAppliedMatch();
                                                        },
                                                        256,
                                                        true);
        const auto retainedAppliedAfterReferenceClear = runProcessorOnFile (inputFile,
                                                                            [] (BolbolRefMasterAudioProcessor& processor)
                                                                            {
                                                                                processor.setPreviewBlendAmount (1.0f);
                                                                                processor.commitPreviewAsAppliedMatch();
                                                                                processor.clearReferenceTrack();
                                                                            },
                                                                            256,
                                                                            true);

        probe.setPreviewEqEnabled (true);
        probe.setPreviewEqBypassed (false);
        probe.setPreviewBlendAmount (0.73f);
        probe.setPreviewOutputGainDb (1.5f);
        probe.commitPreviewAsAppliedMatch();

        juce::MemoryBlock savedState;
        probe.getStateInformation (savedState);

        BolbolRefMasterAudioProcessor restored;
        restored.prepareToPlay (44100.0, 512);
        restored.setStateInformation (savedState.getData(), static_cast<int> (savedState.getSize()));

        const bool restoredReference = restored.hasReferenceTrack();
        const bool restoredPreviewDisabled = ! restored.isPreviewEqEnabled();
        const bool restoredBypassed = restored.isPreviewEqBypassed();
        const bool restoredBlend = std::abs (restored.getPreviewBlendAmount() - 0.73f) < 0.01f;
        const bool restoredGain = std::abs (restored.getPreviewOutputGainDb() - 1.5f) < 0.15f;
        const bool restoredAppliedMatch = restored.hasAppliedMatch();
        const bool restoredAppliedEnabled = restored.isAppliedMatchEnabled();
        restored.prepareToPlay (44100.0, 512);
        const bool restoredAfterPrepareKeepsReference = restored.hasReferenceTrack();
        const bool restoredAfterPrepareKeepsAppliedMatch = restored.hasAppliedMatch();

        BolbolRefMasterAudioProcessor restoreClearsReference;
        restoreClearsReference.prepareToPlay (44100.0, 512);
        restoreClearsReference.loadReferenceFile (inputFile);

        juce::ValueTree noReferenceState ("PARAMETERS");
        noReferenceState.setProperty ("previewEqEnabled", false, nullptr);
        noReferenceState.setProperty ("previewEqBypassed", false, nullptr);
        noReferenceState.setProperty ("previewBlendAmount", 0.5f, nullptr);
        noReferenceState.setProperty ("previewOutputGainDb", 0.0f, nullptr);
        noReferenceState.setProperty ("appliedMatchEnabled", false, nullptr);

        juce::MemoryBlock noReferenceStateBlock;
        if (const auto xml = noReferenceState.createXml())
            BolbolRefMasterAudioProcessor::copyXmlToBinary (*xml, noReferenceStateBlock);

        restoreClearsReference.setStateInformation (noReferenceStateBlock.getData(), static_cast<int> (noReferenceStateBlock.getSize()));
        const bool emptyStateClearsReference = ! restoreClearsReference.hasReferenceTrack();

        auto brokenStateXml = BolbolRefMasterAudioProcessor::getXmlFromBinary (savedState.getData(), static_cast<int> (savedState.getSize()));
        if (brokenStateXml == nullptr)
            throw std::runtime_error ("Failed to decode saved processor state");

        juce::ValueTree brokenState = juce::ValueTree::fromXml (*brokenStateXml);
        brokenState.setProperty ("referenceTrackPath", "/tmp/this-file-should-not-exist.mp3", nullptr);
        juce::MemoryBlock brokenStateBlock;
        if (const auto xml = brokenState.createXml())
            BolbolRefMasterAudioProcessor::copyXmlToBinary (*xml, brokenStateBlock);

        BolbolRefMasterAudioProcessor brokenRestore;
        brokenRestore.prepareToPlay (44100.0, 512);
        brokenRestore.setStateInformation (brokenStateBlock.getData(), static_cast<int> (brokenStateBlock.getSize()));

        const bool brokenStateHasNoReference = ! brokenRestore.hasReferenceTrack();
        const bool brokenStateShowsError = brokenRestore.hasReferenceLoadError();
        const bool brokenStateStillHasAppliedMatch = brokenRestore.hasAppliedMatch();
        const bool brokenStateAppliedEnabled = brokenRestore.isAppliedMatchEnabled();
        const auto restoredAppliedWithoutReference = runProcessorOnFile (inputFile,
                                                                         [&brokenStateBlock] (BolbolRefMasterAudioProcessor& processor)
                                                                         {
                                                                             processor.setStateInformation (brokenStateBlock.getData(),
                                                                                                           static_cast<int> (brokenStateBlock.getSize()));
                                                                         },
                                                                         256,
                                                                         false);

        auto printStats = [] (const char* label, const RunStats& stats)
        {
            std::cout << label
                      << " input_rms=" << stats.inputRms
                      << " output_rms=" << stats.outputRms
                      << " diff_rms=" << stats.diffRms
                      << " max_abs_diff=" << stats.maxAbsDiff
                      << " any_nan=" << (stats.anyNaN ? "true" : "false")
                      << " any_inf=" << (stats.anyInf ? "true" : "false")
                      << "\n";
        };

        printStats ("dry", dry);
        printStats ("wet", wet);
        printStats ("bypassed", bypassed);
        printStats ("zero_blend", zeroBlend);
        printStats ("applied", applied);
        printStats ("applied_bypassed", appliedBypassed);
        printStats ("cleared_applied", clearedApplied);
        printStats ("retained_applied_after_reference_clear", retainedAppliedAfterReferenceClear);
        printStats ("restored_applied_missing_reference", restoredAppliedWithoutReference);

        const bool dryIsTransparent = dry.diffRms < 1.0e-7;
        const bool wetChangesSignal = wet.diffRms > 1.0e-4;
        const bool bypassIsNearDry = std::abs (bypassed.diffRms - dry.diffRms) < 1.0e-5;
        const bool zeroBlendNearDry = zeroBlend.diffRms < 5.0e-4;
        const bool appliedChangesSignal = applied.diffRms > 1.0e-4;
        const bool appliedBypassNearDry = std::abs (appliedBypassed.diffRms - dry.diffRms) < 1.0e-5;
        const bool clearedAppliedNearDry = clearedApplied.diffRms < 5.0e-4;
        const bool retainedAppliedAfterReferenceClearChangesSignal = retainedAppliedAfterReferenceClear.diffRms > 1.0e-4;
        const bool restoredAppliedChangesSignal = restoredAppliedWithoutReference.diffRms > 1.0e-4;
        const bool numericallySafe = ! wet.anyNaN && ! wet.anyInf
                                  && ! bypassed.anyNaN && ! bypassed.anyInf
                                  && ! applied.anyNaN && ! applied.anyInf
                                  && ! restoredAppliedWithoutReference.anyNaN && ! restoredAppliedWithoutReference.anyInf;

        std::cout << "dry_transparent=" << (dryIsTransparent ? "true" : "false") << "\n";
        std::cout << "wet_changes_signal=" << (wetChangesSignal ? "true" : "false") << "\n";
        std::cout << "bypass_near_dry=" << (bypassIsNearDry ? "true" : "false") << "\n";
        std::cout << "zero_blend_near_dry=" << (zeroBlendNearDry ? "true" : "false") << "\n";
        std::cout << "applied_changes_signal=" << (appliedChangesSignal ? "true" : "false") << "\n";
        std::cout << "applied_bypass_near_dry=" << (appliedBypassNearDry ? "true" : "false") << "\n";
        std::cout << "cleared_applied_near_dry=" << (clearedAppliedNearDry ? "true" : "false") << "\n";
        std::cout << "retained_applied_after_reference_clear_changes_signal=" << (retainedAppliedAfterReferenceClearChangesSignal ? "true" : "false") << "\n";
        std::cout << "restored_applied_changes_signal=" << (restoredAppliedChangesSignal ? "true" : "false") << "\n";
        std::cout << "numerically_safe=" << (numericallySafe ? "true" : "false") << "\n";
        std::cout << "state_restored_reference=" << (restoredReference ? "true" : "false") << "\n";
        std::cout << "state_restored_preview_disabled=" << (restoredPreviewDisabled ? "true" : "false") << "\n";
        std::cout << "state_restored_bypassed=" << (restoredBypassed ? "true" : "false") << "\n";
        std::cout << "state_restored_blend=" << (restoredBlend ? "true" : "false") << "\n";
        std::cout << "state_restored_gain=" << (restoredGain ? "true" : "false") << "\n";
        std::cout << "state_restored_applied_match=" << (restoredAppliedMatch ? "true" : "false") << "\n";
        std::cout << "state_restored_applied_enabled=" << (restoredAppliedEnabled ? "true" : "false") << "\n";
        std::cout << "state_restored_after_prepare_keeps_reference=" << (restoredAfterPrepareKeepsReference ? "true" : "false") << "\n";
        std::cout << "state_restored_after_prepare_keeps_applied_match=" << (restoredAfterPrepareKeepsAppliedMatch ? "true" : "false") << "\n";
        std::cout << "empty_state_clears_reference=" << (emptyStateClearsReference ? "true" : "false") << "\n";
        std::cout << "broken_state_has_no_reference=" << (brokenStateHasNoReference ? "true" : "false") << "\n";
        std::cout << "broken_state_shows_error=" << (brokenStateShowsError ? "true" : "false") << "\n";
        std::cout << "broken_state_has_applied_match=" << (brokenStateStillHasAppliedMatch ? "true" : "false") << "\n";
        std::cout << "broken_state_applied_enabled=" << (brokenStateAppliedEnabled ? "true" : "false") << "\n";

        return (loaded
             && dryIsTransparent
             && wetChangesSignal
             && bypassIsNearDry
             && zeroBlendNearDry
             && appliedChangesSignal
             && appliedBypassNearDry
             && clearedAppliedNearDry
             && retainedAppliedAfterReferenceClearChangesSignal
             && restoredAppliedChangesSignal
             && numericallySafe
             && restoredReference
             && restoredPreviewDisabled
             && ! restoredBypassed
             && restoredBlend
             && restoredGain
             && restoredAppliedMatch
             && restoredAppliedEnabled
             && restoredAfterPrepareKeepsReference
             && restoredAfterPrepareKeepsAppliedMatch
             && emptyStateClearsReference
             && brokenStateHasNoReference
             && brokenStateShowsError
             && brokenStateStillHasAppliedMatch
             && brokenStateAppliedEnabled) ? 0 : 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "processor_smoke_exception=" << e.what() << "\n";
        return 3;
    }
}
