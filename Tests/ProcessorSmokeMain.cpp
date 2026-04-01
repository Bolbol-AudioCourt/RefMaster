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

static RunStats runProcessorOnFile (const juce::File& inputFile,
                                    bool previewEnabled,
                                    bool previewBypassed,
                                    float previewBlend,
                                    int maxBlocks)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (inputFile));
    if (reader == nullptr)
        throw std::runtime_error ("Failed to open source audio file");

    BolbolRefMasterAudioProcessor processor;
    processor.prepareToPlay (reader->sampleRate, 512);

    if (! processor.loadReferenceFile (inputFile))
        throw std::runtime_error ("Processor failed to load reference file");

    processor.setPreviewBlendAmount (previewBlend);
    processor.setPreviewEqEnabled (previewEnabled);
    processor.setPreviewEqBypassed (previewBypassed);

    const int numChannels = juce::jmin (2, static_cast<int> (reader->numChannels));
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

    for (juce::int64 start = 0; start < reader->lengthInSamples && blocks < maxBlocks; start += ioBuffer.getNumSamples(), ++blocks)
    {
        const int numToRead = static_cast<int> (juce::jmin<juce::int64> (ioBuffer.getNumSamples(), reader->lengthInSamples - start));
        ioBuffer.clear();
        dryCopy.clear();
        reader->read (&ioBuffer, 0, numToRead, start, true, true);
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

int main (int argc, char* argv[])
{
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

        const auto dry = runProcessorOnFile (inputFile, false, false, 0.5f, 256);
        const auto wet = runProcessorOnFile (inputFile, true, false, 0.5f, 256);
        const auto bypassed = runProcessorOnFile (inputFile, true, true, 0.5f, 256);
        const auto zeroBlend = runProcessorOnFile (inputFile, true, false, 0.0f, 256);

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

        const bool dryIsTransparent = dry.diffRms < 1.0e-7;
        const bool wetChangesSignal = wet.diffRms > 1.0e-4;
        const bool bypassIsNearDry = std::abs (bypassed.diffRms - dry.diffRms) < 1.0e-5;
        const bool zeroBlendNearDry = zeroBlend.diffRms < 5.0e-4;
        const bool numericallySafe = ! wet.anyNaN && ! wet.anyInf && ! bypassed.anyNaN && ! bypassed.anyInf;

        std::cout << "dry_transparent=" << (dryIsTransparent ? "true" : "false") << "\n";
        std::cout << "wet_changes_signal=" << (wetChangesSignal ? "true" : "false") << "\n";
        std::cout << "bypass_near_dry=" << (bypassIsNearDry ? "true" : "false") << "\n";
        std::cout << "zero_blend_near_dry=" << (zeroBlendNearDry ? "true" : "false") << "\n";
        std::cout << "numerically_safe=" << (numericallySafe ? "true" : "false") << "\n";

        return (loaded && dryIsTransparent && wetChangesSignal && bypassIsNearDry && zeroBlendNearDry && numericallySafe) ? 0 : 2;
    }
    catch (const std::exception& e)
    {
        std::cerr << "processor_smoke_exception=" << e.what() << "\n";
        return 3;
    }
}
