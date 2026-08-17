#include "dspstatus.h"

#include "audiopipeline.h"
#include "eqprocessor.h"
#include "resampler.h"
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

int runVerificationChecks(bool includeRingBufferStress)
{
    dspPrint(false, "Running DSP verification checks...\n");
    int failures = 0;
    failures += runEqImpulseTest();
    failures += runFlatEqPassthroughTest();
    failures += runPipelineSmokeTest();
    failures += runResamplerTest();
    failures += runResamplerContinuityTest();
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
    dspPrint(false, "  Pipeline         : AudioProcessor chain (EQ -> surround optional)\n");
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
