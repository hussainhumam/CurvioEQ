#include "dspstatus.h"

#include "audiopipeline.h"
#include "eqprocessor.h"
#include "resampler.h"
#include "hrtfpresets.h"
#include "virtualsurroundprocessor.h"
#include "virtualsurroundsettings.h"
#include "dynamicsprocessor.h"
#include "dynamicrangesettings.h"
#include "loudnessprocessor.h"

#include "spscringbuffer.h"
#include "ui/appconstants.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

bool g_useWin32Console = false;
std::vector<std::string> *g_reportLines = nullptr;

void appendReportLine(const char *text)
{
    if (!g_reportLines || !text) {
        return;
    }

    std::string line(text);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    if (!line.empty()) {
        g_reportLines->push_back(std::move(line));
    }
}

void dspPrint(bool isError, const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    appendReportLine(buffer);

    if (g_useWin32Console) {
        HANDLE handle = GetStdHandle(isError ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            const DWORD length = static_cast<DWORD>(strlen(buffer));
            DWORD mode = 0;
            if (GetConsoleMode(handle, &mode)) {
                WriteConsoleA(handle, buffer, length, &written, nullptr);
            } else {
                WriteFile(handle, buffer, length, &written, nullptr);
            }
            return;
        }
    }

    FILE *stream = isError ? stderr : stdout;
    fputs(buffer, stream);
    fflush(stream);
}

bool nearlyEqual(float a, float b, float epsilon = 1e-3f)
{
    return std::fabs(a - b) <= epsilon;
}

int runEqImpulseTest()
{
    EqProcessor eq;
    eq.setSampleRate(48000.f);
    std::array<float, EqProcessor::kBandCount> gains{};
    gains[4] = 12.f;
    eq.setGains(gains);

    std::vector<float> impulse(48000 * 2, 0.f);
    impulse[0] = 1.f;
    eq.process(impulse.data(), 48000, 2);

    float peak = 0.f;
    for (float sample : impulse) {
        peak = std::max(peak, std::fabs(sample));
    }

    if (peak <= 1.f) {
        dspPrint(true, "  [FAIL] EQ impulse (peak=%f)\n", peak);
        return 1;
    }

    dspPrint(false, "  [OK]   EQ impulse (peak=%.3f)\n", peak);
    return 0;
}

int runFlatEqPassthroughTest()
{
    EqProcessor eq;
    eq.setSampleRate(48000.f);

    std::vector<float> impulse(256 * 2, 0.f);
    impulse[0] = 1.f;
    eq.process(impulse.data(), 256, 2);

    if (!nearlyEqual(impulse[0], 1.f, 0.05f)) {
        dspPrint(true, "  [FAIL] flat EQ pass-through (peak=%f)\n", impulse[0]);
        return 1;
    }

    dspPrint(false, "  [OK]   flat EQ pass-through\n");
    return 0;
}

int runPipelineSmokeTest()
{
    EqProcessor eq;
    AudioPipeline pipeline;
    pipeline.addProcessor(&eq);
    pipeline.setSampleRate(48000.f);

    std::vector<float> buffer(128 * 2, 0.f);
    buffer[0] = 0.5f;
    buffer[1] = 0.5f;
    pipeline.process(buffer.data(), 128, 2);

    if (!std::isfinite(buffer[0]) || !std::isfinite(buffer[1])) {
        dspPrint(true, "  [FAIL] audio pipeline smoke\n");
        return 1;
    }

    dspPrint(false, "  [OK]   audio pipeline smoke\n");
    return 0;
}

int runResamplerTest()
{
    Resampler resampler;
    resampler.configure(44100.f, 48000.f, 2);

    std::vector<float> input(441 * 2);
    for (int i = 0; i < 441; ++i) {
        const float sample = std::sin(2.f * 3.14159265358979323846f * 440.f * static_cast<float>(i) / 44100.f);
        input[static_cast<size_t>(i * 2)] = sample;
        input[static_cast<size_t>(i * 2 + 1)] = sample;
    }

    std::vector<float> output(480 * 2);
    const int outFrames = resampler.process(input.data(), 441, output.data(), 480);
    if (outFrames <= 0) {
        dspPrint(true, "  [FAIL] resampler (44100 -> 48000 Hz)\n");
        return 1;
    }

    dspPrint(false, "  [OK]   resampler (441 -> %d frames @ 48 kHz)\n", outFrames);
    return 0;
}

int runResamplerContinuityTest()
{
    constexpr int kChunkFrames = 512;
    constexpr int kChannels = 2;

    std::vector<float> chunk(static_cast<size_t>(kChunkFrames * kChannels));
    for (int i = 0; i < kChunkFrames; ++i) {
        const float sample =
            std::sin(2.f * 3.14159265358979323846f * 440.f * static_cast<float>(i) / 44100.f);
        chunk[static_cast<size_t>(i * kChannels)] = sample;
        chunk[static_cast<size_t>(i * kChannels + 1)] = sample;
    }

    std::vector<float> nextChunk = chunk;
    for (int i = 0; i < kChunkFrames; ++i) {
        const float sample = std::sin(2.f * 3.14159265358979323846f * 440.f
                                      * static_cast<float>(kChunkFrames + i) / 44100.f);
        nextChunk[static_cast<size_t>(i * kChannels)] = sample;
        nextChunk[static_cast<size_t>(i * kChannels + 1)] = sample;
    }

    Resampler resampler;
    resampler.configure(44100.f, 48000.f, kChannels);

    std::vector<float> firstOutput(static_cast<size_t>(kChunkFrames * kChannels * 2), 0.f);
    std::vector<float> secondOutput(static_cast<size_t>(kChunkFrames * kChannels * 2), 0.f);

    const int firstFrames = resampler.process(chunk.data(), kChunkFrames, firstOutput.data(), kChunkFrames * 2);
    const int secondFrames =
        resampler.process(nextChunk.data(), kChunkFrames, secondOutput.data(), kChunkFrames * 2);
    if (firstFrames <= 1 || secondFrames <= 1) {
        dspPrint(true, "  [FAIL] resampler chunk continuity (empty chunk)\n");
        return 1;
    }

    float joinDelta = 0.f;
    for (int channel = 0; channel < kChannels; ++channel) {
        const size_t lastIndex = static_cast<size_t>((firstFrames - 1) * kChannels + channel);
        const size_t firstIndex = static_cast<size_t>(channel);
        joinDelta = std::max(joinDelta, std::fabs(secondOutput[firstIndex] - firstOutput[lastIndex]));
    }

    float maxNeighborDelta = 0.f;
    for (int frame = 1; frame < secondFrames; ++frame) {
        for (int channel = 0; channel < kChannels; ++channel) {
            const size_t index = static_cast<size_t>(frame * kChannels + channel);
            maxNeighborDelta =
                std::max(maxNeighborDelta, std::fabs(secondOutput[index] - secondOutput[index - kChannels]));
        }
    }

    if (joinDelta > maxNeighborDelta * 4.f + 0.05f) {
        dspPrint(true, "  [FAIL] resampler chunk continuity (joinDelta=%.4f, neighbor=%.4f)\n",
                 joinDelta,
                 maxNeighborDelta);
        return 1;
    }

    dspPrint(false, "  [OK]   resampler chunk continuity\n");
    return 0;
}

int runRingBufferQuickTest()
{
    SpscRingBuffer ring;
    ring.configure(2, 256);

    std::vector<float> chunk(64 * 2, 0.25f);
    ring.write(chunk.data(), 64);

    std::vector<float> dest(64 * 2, 0.f);
    const int read = ring.readAdd(dest.data(), 64, 2);
    if (read != 64) {
        dspPrint(true, "  [FAIL] SPSC ring buffer quick check\n");
        return 1;
    }

    if (std::fabs(dest[0] - 0.25f) > 1e-4f) {
        dspPrint(true, "  [FAIL] SPSC ring buffer sample mismatch\n");
        return 1;
    }

    dspPrint(false, "  [OK]   SPSC ring buffer quick check\n");
    return 0;
}

int runRingBufferStressTest()
{
    SpscRingBuffer ring;
    ring.configure(2, 1024);

    std::atomic<bool> stop{false};
    std::atomic<int> writeErrors{0};

    std::thread writer([&]() {
        std::vector<float> chunk(128 * 2, 0.25f);
        while (!stop.load()) {
            ring.write(chunk.data(), 128);
        }
    });

    std::thread reader([&]() {
        std::vector<float> dest(256 * 2, 0.f);
        while (!stop.load()) {
            const int read = ring.readAdd(dest.data(), 256, 2);
            if (read < 0) {
                ++writeErrors;
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop.store(true);
    writer.join();
    reader.join();

    if (writeErrors.load() != 0) {
        dspPrint(true, "  [FAIL] SPSC ring buffer stress\n");
        return 1;
    }

    dspPrint(false, "  [OK]   SPSC ring buffer stress (2 s)\n");
    return 0;
}

int runVirtualSurroundPassthroughTest()
{
    VirtualSurroundProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(false);

    std::vector<float> buffer(256 * 2, 0.f);
    buffer[0] = 0.75f;
    buffer[1] = -0.25f;
    processor.process(buffer.data(), buffer.data(), 256);

    if (!nearlyEqual(buffer[0], 0.75f, 0.001f) || !nearlyEqual(buffer[1], -0.25f, 0.001f)) {
        dspPrint(true, "  [FAIL] virtual surround disabled passthrough\n");
        return 1;
    }

    dspPrint(false, "  [OK]   virtual surround disabled passthrough\n");
    return 0;
}

int runVirtualSurroundSpatialTest()
{
    VirtualSurroundProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setStrength(100);
    processor.setPreset(static_cast<int>(HrtfPresetId::Default));

    std::array<int, VirtualSurroundProcessor::kSpeakerCount> rearBoost{};
    rearBoost.fill(50);
    rearBoost[SurroundProcessor::BackLeft] = 100;
    rearBoost[SurroundProcessor::FrontLeft] = 0;
    rearBoost[SurroundProcessor::FrontRight] = 0;
    rearBoost[SurroundProcessor::FrontCenter] = 0;
    processor.setChannelLevels(rearBoost);

    std::vector<float> impulse(4096 * 2, 0.f);
    impulse[0] = 1.f;
    impulse[1] = 1.f;
    std::vector<float> output(impulse.size(), 0.f);
    processor.process(impulse.data(), output.data(), 4096);

    float leftEnergy = 0.f;
    float rightEnergy = 0.f;
    for (int frame = 0; frame < 4096; ++frame) {
        leftEnergy += std::fabs(output[static_cast<size_t>(frame * 2)]);
        rightEnergy += std::fabs(output[static_cast<size_t>(frame * 2 + 1)]);
    }

    if (leftEnergy <= 1e-4f && rightEnergy <= 1e-4f) {
        dspPrint(true, "  [FAIL] virtual surround spatial impulse (silent output)\n");
        return 1;
    }

    dspPrint(false,
             "  [OK]   virtual surround spatial impulse (L=%.3f R=%.3f latency=%d frames)\n",
             leftEnergy,
             rightEnergy,
             processor.latencyFrames());
    return 0;
}

int runVirtualSurroundEnabledSmokeTest()
{
    VirtualSurroundProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setStrength(80);
    processor.setPreset(static_cast<int>(HrtfPresetId::Wide));

    std::vector<float> input(512 * 2);
    for (int i = 0; i < 512; ++i) {
        const float sample = std::sin(2.f * 3.14159265358979323846f * 220.f * static_cast<float>(i) / 48000.f);
        input[static_cast<size_t>(i * 2)] = sample;
        input[static_cast<size_t>(i * 2 + 1)] = sample * 0.5f;
    }

    std::vector<float> output(input.size(), 0.f);
    processor.process(input.data(), output.data(), 512);

    for (float sample : output) {
        if (!std::isfinite(sample)) {
            dspPrint(true, "  [FAIL] virtual surround enabled smoke (non-finite sample)\n");
            return 1;
        }
    }

    dspPrint(false, "  [OK]   virtual surround enabled smoke\n");
    return 0;
}

int runVirtualSurroundLowFrequencyTest()
{
    VirtualSurroundProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setStrength(75);
    processor.setPreset(static_cast<int>(HrtfPresetId::Default));
    processor.setChannelLevels(defaultVirtualSurroundChannelLevels());

    constexpr int kFrames = 4096;
    std::vector<float> sine(kFrames * 2);
    float dryEnergy = 0.f;
    for (int i = 0; i < kFrames; ++i) {
        const float sample = std::sin(2.f * 3.14159265358979323846f * 440.f * static_cast<float>(i) / 48000.f);
        sine[static_cast<size_t>(i * 2)] = sample;
        sine[static_cast<size_t>(i * 2 + 1)] = sample;
        dryEnergy += sample * sample;
    }

    std::vector<float> output(sine.size(), 0.f);
    processor.process(sine.data(), output.data(), kFrames);

    float wetEnergy = 0.f;
    for (int i = 0; i < kFrames; ++i) {
        const float left = output[static_cast<size_t>(i * 2)];
        const float right = output[static_cast<size_t>(i * 2 + 1)];
        wetEnergy += left * left + right * right;
    }

    const float dryRms = std::sqrt(dryEnergy / static_cast<float>(kFrames * 2));
    const float wetRms = std::sqrt(wetEnergy / static_cast<float>(kFrames * 2));
    const float ratio = wetRms / std::max(dryRms, 1e-6f);

    if (ratio > 2.5f) {
        dspPrint(true, "  [FAIL] virtual surround LF gain (ratio=%.3f)\n", ratio);
        return 1;
    }

    dspPrint(false, "  [OK]   virtual surround LF gain (ratio=%.3f)\n", ratio);
    return 0;
}

int runVirtualSurroundNoiseStabilityTest()
{
    VirtualSurroundProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setStrength(75);
    processor.setChannelLevels(defaultVirtualSurroundChannelLevels());

    std::vector<float> noise(4096 * 2);
    for (size_t i = 0; i < noise.size(); ++i) {
        const float t = static_cast<float>(i) * 0.013f;
        noise[i] = std::sin(t) * 0.25f + std::sin(t * 0.37f) * 0.15f;
    }

    std::vector<float> output(noise.size(), 0.f);
    processor.process(noise.data(), output.data(), 4096);

    float peak = 0.f;
    for (float sample : output) {
        if (!std::isfinite(sample)) {
            dspPrint(true, "  [FAIL] virtual surround noise stability (non-finite)\n");
            return 1;
        }
        peak = std::max(peak, std::fabs(sample));
    }

    if (peak > 1.5f) {
        dspPrint(true, "  [FAIL] virtual surround noise stability (peak=%.3f)\n", peak);
        return 1;
    }

    dspPrint(false, "  [OK]   virtual surround noise stability (peak=%.3f)\n", peak);
    return 0;
}

int runVirtualSurroundBassPreservationTest()
{
    VirtualSurroundProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setChannelLevels(defaultVirtualSurroundChannelLevels());

    constexpr int kFrames = 4800;
    std::vector<float> sine(kFrames * 2);
    float inputEnergy = 0.f;
    for (int i = 0; i < kFrames; ++i) {
        const float sample = 0.5f * std::sin(2.f * 3.14159265358979323846f * 60.f * static_cast<float>(i) / 48000.f);
        sine[static_cast<size_t>(i * 2)] = sample;
        sine[static_cast<size_t>(i * 2 + 1)] = sample;
        inputEnergy += sample * sample + sample * sample;
    }

    auto measureRatio = [&](int strength) {
        processor.setStrength(strength);
        std::vector<float> output(sine.size(), 0.f);
        processor.process(sine.data(), output.data(), kFrames);

        float outputEnergy = 0.f;
        for (int i = kFrames / 2; i < kFrames; ++i) {
            const float left = output[static_cast<size_t>(i * 2)];
            const float right = output[static_cast<size_t>(i * 2 + 1)];
            outputEnergy += left * left + right * right;
        }

        const float inputRms = std::sqrt(inputEnergy / static_cast<float>(kFrames * 2));
        const float outputRms = std::sqrt(outputEnergy / static_cast<float>((kFrames / 2) * 2));
        return outputRms / std::max(inputRms, 1e-6f);
    };

    const float dryRatio = measureRatio(0);
    if (dryRatio < 0.98f || dryRatio > 1.02f) {
        dspPrint(true, "  [FAIL] virtual surround bass preservation dry (ratio=%.3f)\n", dryRatio);
        return 1;
    }

    const float ratio = measureRatio(75);
    if (ratio < 0.85f || ratio > 1.2f) {
        dspPrint(true, "  [FAIL] virtual surround bass preservation (ratio=%.3f)\n", ratio);
        return 1;
    }

    dspPrint(false, "  [OK]   virtual surround bass preservation (ratio=%.3f)\n", ratio);
    return 0;
}

int runDynamicsBypassTest()
{
    DynamicsProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(false);
    processor.setAmount(50);

    constexpr int kFrames = 512;
    std::vector<float> buffer(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float sample = 0.4f * std::sin(2.f * 3.14159265358979323846f * 440.f * static_cast<float>(i) / 48000.f);
        buffer[static_cast<size_t>(i * 2)] = sample;
        buffer[static_cast<size_t>(i * 2 + 1)] = sample * 0.8f;
    }

    std::vector<float> output = buffer;
    processor.process(output.data(), kFrames, 2);

    for (size_t i = 0; i < buffer.size(); ++i) {
        if (!nearlyEqual(buffer[i], output[i])) {
            dspPrint(true, "  [FAIL] dynamics disabled passthrough\n");
            return 1;
        }
    }

    dspPrint(false, "  [OK]   dynamics disabled passthrough\n");
    return 0;
}

int runDynamicsWideCrestTest()
{
    DynamicsProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setAmount(DynamicRangeSettings::kAmountMin);

    constexpr int kFrames = 4800;
    std::vector<float> input(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float t = static_cast<float>(i) / 48000.f;
        const float low = 0.35f * std::sin(2.f * 3.14159265358979323846f * 60.f * t);
        const float burst = (i % 480) < 48
            ? 0.75f * std::sin(2.f * 3.14159265358979323846f * 1000.f * t)
            : 0.f;
        const float sample = low + burst;
        input[static_cast<size_t>(i * 2)] = sample;
        input[static_cast<size_t>(i * 2 + 1)] = sample;
    }

    auto measureCrest = [](const std::vector<float> &signal, int startFrame, int endFrame) {
        float peak = 0.f;
        float energy = 0.f;
        for (int i = startFrame; i < endFrame; ++i) {
            const float left = signal[static_cast<size_t>(i * 2)];
            const float right = signal[static_cast<size_t>(i * 2 + 1)];
            peak = std::max(peak, std::fabs(left));
            peak = std::max(peak, std::fabs(right));
            energy += left * left + right * right;
        }
        const float rms = std::sqrt(energy / static_cast<float>((endFrame - startFrame) * 2));
        return peak / std::max(rms, 1e-6f);
    };

    const float inputCrest = measureCrest(input, kFrames / 2, kFrames);

    std::vector<float> output = input;
    processor.process(output.data(), kFrames, 2);
    const float outputCrest = measureCrest(output, kFrames / 2, kFrames);
    const float delta = std::fabs(outputCrest - inputCrest) / std::max(inputCrest, 1e-6f);

    if (delta > 0.10f) {
        dspPrint(true, "  [FAIL] dynamics wide crest preservation (delta=%.3f)\n", delta);
        return 1;
    }

    dspPrint(false, "  [OK]   dynamics wide crest preservation (delta=%.3f)\n", delta);
    return 0;
}

int runDynamicsTightPeakTest()
{
    DynamicsProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setAmount(DynamicRangeSettings::kAmountMax);

    constexpr int kFrames = 4800;
    std::vector<float> input(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float t = static_cast<float>(i) / 48000.f;
        const float low = 0.35f * std::sin(2.f * 3.14159265358979323846f * 60.f * t);
        const float burst = (i % 480) < 48
            ? 0.85f * std::sin(2.f * 3.14159265358979323846f * 1000.f * t)
            : 0.f;
        const float sample = low + burst;
        input[static_cast<size_t>(i * 2)] = sample;
        input[static_cast<size_t>(i * 2 + 1)] = sample;
    }

    float inputPeak = 0.f;
    for (int i = kFrames / 2; i < kFrames; ++i) {
        inputPeak = std::max(inputPeak, std::fabs(input[static_cast<size_t>(i * 2)]));
    }

    std::vector<float> output = input;
    processor.process(output.data(), kFrames, 2);

    float outputPeak = 0.f;
    for (int i = kFrames / 2; i < kFrames; ++i) {
        const float left = output[static_cast<size_t>(i * 2)];
        const float right = output[static_cast<size_t>(i * 2 + 1)];
        if (!std::isfinite(left) || !std::isfinite(right)) {
            dspPrint(true, "  [FAIL] dynamics tight peak (non-finite)\n");
            return 1;
        }
        outputPeak = std::max(outputPeak, std::fabs(left));
        outputPeak = std::max(outputPeak, std::fabs(right));
    }

    const float reduction = 1.f - (outputPeak / std::max(inputPeak, 1e-6f));
    if (reduction < 0.05f || outputPeak > 1.05f) {
        dspPrint(true, "  [FAIL] dynamics tight peak (reduction=%.3f peak=%.3f)\n", reduction, outputPeak);
        return 1;
    }

    dspPrint(false, "  [OK]   dynamics tight peak (reduction=%.3f peak=%.3f)\n", reduction, outputPeak);
    return 0;
}

int runDynamicsNoiseStabilityTest()
{
    DynamicsProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setAmount(75);

    std::vector<float> noise(4096 * 2);
    for (size_t i = 0; i < noise.size(); ++i) {
        const float t = static_cast<float>(i) * 0.017f;
        noise[i] = std::sin(t) * 0.35f + std::sin(t * 0.41f) * 0.2f;
    }

    processor.process(noise.data(), 4096, 2);

    float peak = 0.f;
    for (float sample : noise) {
        if (!std::isfinite(sample)) {
            dspPrint(true, "  [FAIL] dynamics noise stability (non-finite)\n");
            return 1;
        }
        peak = std::max(peak, std::fabs(sample));
    }

    if (peak > 1.5f) {
        dspPrint(true, "  [FAIL] dynamics noise stability (peak=%.3f)\n", peak);
        return 1;
    }

    dspPrint(false, "  [OK]   dynamics noise stability (peak=%.3f)\n", peak);
    return 0;
}

int runLoudnessBypassTest()
{
    LoudnessProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setAmount(DynamicRangeSettings::kLoudnessMin);

    constexpr int kFrames = 512;
    std::vector<float> buffer(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float sample = 0.4f * std::sin(2.f * 3.14159265358979323846f * 440.f * static_cast<float>(i) / 48000.f);
        buffer[static_cast<size_t>(i * 2)] = sample;
        buffer[static_cast<size_t>(i * 2 + 1)] = sample * 0.8f;
    }

    std::vector<float> output = buffer;
    processor.process(output.data(), kFrames, 2);

    for (size_t i = 0; i < buffer.size(); ++i) {
        if (!nearlyEqual(buffer[i], output[i])) {
            dspPrint(true, "  [FAIL] loudness bypass at zero\n");
            return 1;
        }
    }

    dspPrint(false, "  [OK]   loudness bypass at zero\n");
    return 0;
}

int runLoudnessQuietBoostTest()
{
    LoudnessProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setAmount(DynamicRangeSettings::kLoudnessMax);

    constexpr int kFrames = 48000;
    constexpr float kQuietAmplitude = 0.03162f; // ~ -30 dBFS
    std::vector<float> buffer(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float sample = kQuietAmplitude * std::sin(2.f * 3.14159265358979323846f * 440.f * static_cast<float>(i) / 48000.f);
        buffer[static_cast<size_t>(i * 2)] = sample;
        buffer[static_cast<size_t>(i * 2 + 1)] = sample;
    }

    auto measureRms = [](const std::vector<float> &signal, int startFrame, int endFrame) {
        float energy = 0.f;
        for (int i = startFrame; i < endFrame; ++i) {
            const float left = signal[static_cast<size_t>(i * 2)];
            const float right = signal[static_cast<size_t>(i * 2 + 1)];
            energy += left * left + right * right;
        }
        return std::sqrt(energy / static_cast<float>((endFrame - startFrame) * 2));
    };

    const float inputRms = measureRms(buffer, kFrames / 2, kFrames);
    processor.process(buffer.data(), kFrames, 2);
    const float outputRms = measureRms(buffer, kFrames / 2, kFrames);

    for (float sample : buffer) {
        if (!std::isfinite(sample)) {
            dspPrint(true, "  [FAIL] loudness quiet boost (non-finite)\n");
            return 1;
        }
    }

    const float boostDb = 20.f * std::log10(outputRms / std::max(inputRms, 1e-9f));
    if (boostDb < 6.f) {
        dspPrint(true, "  [FAIL] loudness quiet boost (boost=%.2f dB)\n", boostDb);
        return 1;
    }

    dspPrint(false, "  [OK]   loudness quiet boost (boost=%.2f dB)\n", boostDb);
    return 0;
}

int runLoudnessHotLimitTest()
{
    LoudnessProcessor processor;
    processor.setSampleRate(48000.f);
    processor.setEnabled(true);
    processor.setAmount(DynamicRangeSettings::kLoudnessMax);

    constexpr int kFrames = 48000;
    std::vector<float> buffer(kFrames * 2);
    for (int i = 0; i < kFrames; ++i) {
        const float sample = std::sin(2.f * 3.14159265358979323846f * 440.f * static_cast<float>(i) / 48000.f);
        buffer[static_cast<size_t>(i * 2)] = sample;
        buffer[static_cast<size_t>(i * 2 + 1)] = sample;
    }

    processor.process(buffer.data(), kFrames, 2);

    float peak = 0.f;
    for (float sample : buffer) {
        if (!std::isfinite(sample)) {
            dspPrint(true, "  [FAIL] loudness hot limit (non-finite)\n");
            return 1;
        }
        peak = std::max(peak, std::fabs(sample));
    }

    if (peak > 0.99f) {
        dspPrint(true, "  [FAIL] loudness hot limit (peak=%.3f)\n", peak);
        return 1;
    }

    dspPrint(false, "  [OK]   loudness hot limit (peak=%.3f)\n", peak);
    return 0;
}

int runVerificationChecks(bool includeRingBufferStress)
{
    dspPrint(false, "Running DSP verification checks...\n");
    int failures = 0;
    failures += runEqImpulseTest();
    failures += runFlatEqPassthroughTest();
    failures += runPipelineSmokeTest();
    failures += runResamplerTest();
    failures += runResamplerContinuityTest();
    failures += runVirtualSurroundPassthroughTest();
    failures += runVirtualSurroundSpatialTest();
    failures += runVirtualSurroundEnabledSmokeTest();
    failures += runVirtualSurroundLowFrequencyTest();
    failures += runVirtualSurroundBassPreservationTest();
    failures += runVirtualSurroundNoiseStabilityTest();
    failures += runDynamicsBypassTest();
    failures += runDynamicsWideCrestTest();
    failures += runDynamicsTightPeakTest();
    failures += runDynamicsNoiseStabilityTest();
    failures += runLoudnessBypassTest();
    failures += runLoudnessQuietBoostTest();
    failures += runLoudnessHotLimitTest();
    failures += includeRingBufferStress ? runRingBufferStressTest() : runRingBufferQuickTest();
    dspPrint(false, "\n");

    if (failures == 0) {
        dspPrint(false, "DSP state: HEALTHY (all checks passed)\n");
    } else {
        dspPrint(true, "DSP state: UNHEALTHY (%d check(s) failed)\n", failures);
    }

    return failures;
}

} // namespace

void setDspStatusWin32ConsoleOutput(bool enabled)
{
    g_useWin32Console = enabled;
}

void printDspArchitectureState()
{
    dspPrint(false, "=== CurvioEQ DSP State ===\n");
    dspPrint(false, "  EQ topology      : parallel peaking (%d bands, +/-%d dB)\n",
               EqProcessor::kBandCount,
               AppConstants::kMaxGainDb);
    dspPrint(false, "  Resampler        : cubic Hermite\n");
    dspPrint(false, "  Session I/O      : lock-free SPSC ring buffer\n");
    dspPrint(false, "  Ring capacity    : %d frames (target fill %d, high %d)\n",
               AppConstants::kSessionRingBufferFrames,
               AppConstants::kTargetRingFillFrames,
               AppConstants::kHighRingFillFrames);
    dspPrint(false, "  Mix bus          : soft-knee limiter\n");
    dspPrint(false, "  Pipeline         : EQ -> optional HRTF -> optional dynamics -> optional loudness (stereo)\n");
    dspPrint(false, "  Virtual surround : 8-speaker upmix + HRIR convolution\n");
    dspPrint(false, "  Dynamic range    : stereo-linked soft-knee compressor (Wide/Tight)\n");
    dspPrint(false, "  Loudness         : K-weighted gain rider (-24 to -14 LUFS target)\n");
    dspPrint(false, "  Thread hygiene   : FTZ/DAZ + Pro Audio mixer thread\n");
    dspPrint(false, "==========================\n");
}

int runDspVerification()
{
    const int failures = runVerificationChecks(true);
    dspPrint(false, "\n");
    return failures;
}

DspStatusReport collectDspStatusReport(bool includeRingBufferStress)
{
    DspStatusReport report;
    g_reportLines = &report.lines;
    printDspArchitectureState();
    report.failureCount = runVerificationChecks(includeRingBufferStress);
    g_reportLines = nullptr;
    return report;
}
