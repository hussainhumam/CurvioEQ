#include "clipfrequencyanalyzer.h"

#include "audiothreadutils.h"
#include "processloopbackcapture.h"
#include "ui/spectrumanalyzer.h"

#define WIN32_LEAN_AND_MEAN
#include <objbase.h>
#include <windows.h>
#include <winerror.h>

#include <QMetaObject>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr int kFrameChunk = 512;
constexpr int kFftSize = SpectrumCapture::kFftSize;
constexpr float kMaxSeconds = 15.f;
constexpr float kQuietRelative = 0.08f;
constexpr float kPeakRelative = 0.40f;
constexpr float kMinHz = 20.f;
constexpr float kMaxHz = 16000.f;
constexpr float kMinOctaveSeparation = 0.25f;
constexpr int kMaxPeaks = 5;
constexpr float kPi = 3.14159265358979323846f;

float mixdownSample(const float *samples, int frameIndex, int channelCount)
{
    float sum = 0.f;
    for (int channel = 0; channel < channelCount; ++channel) {
        sum += samples[static_cast<size_t>(frameIndex * channelCount + channel)];
    }
    return sum / static_cast<float>(std::max(1, channelCount));
}

float hopRms(const float *samples, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const double s = static_cast<double>(samples[i]);
        sum += s * s;
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(std::max(1, count))));
}

QString formatHz(float hz)
{
    if (hz >= 1000.f) {
        return QStringLiteral("%1 kHz").arg(hz / 1000.f, 0, 'f', 1);
    }
    return QStringLiteral("%1 Hz").arg(hz, 0, 'f', 0);
}

float binFrequency(int bin, int fftSize, float sampleRate)
{
    return static_cast<float>(bin) * sampleRate / static_cast<float>(fftSize);
}

bool tooCloseInOctaves(float aHz, float bHz)
{
    const float lo = std::min(aHz, bHz);
    const float hi = std::max(aHz, bHz);
    if (lo <= 1.f) {
        return true;
    }
    return std::log2(hi / lo) < kMinOctaveSeparation;
}
} // namespace

ClipFrequencyAnalyzer::ClipFrequencyAnalyzer(QObject *parent)
    : QObject(parent)
{
}

ClipFrequencyAnalyzer::~ClipFrequencyAnalyzer()
{
    m_stopRequested.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

QString ClipFrequencyAnalyzer::recordingDisplayName() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_displayName;
}

void ClipFrequencyAnalyzer::emitRecordingChanged()
{
    QString name;
    unsigned long pid = 0;
    const bool recording = m_recording.load();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        name = m_displayName;
        pid = m_processId;
    }
    emit recordingChanged(recording, recording ? pid : 0, name);
}

void ClipFrequencyAnalyzer::start(unsigned long processId, const QString &displayName)
{
    if (processId == 0) {
        emit logMessage(QStringLiteral("WARN"), QStringLiteral("No app selected to record"));
        return;
    }
    if (m_recording.load()) {
        emit logMessage(QStringLiteral("WARN"),
                        QStringLiteral("Already recording a clip from %1").arg(recordingDisplayName()));
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_processId = processId;
        m_displayName = displayName.isEmpty() ? QStringLiteral("App") : displayName;
        m_mono.clear();
        m_sampleRate = 48000.f;
        m_captureError.clear();
    }

    m_stopRequested.store(false);
    m_recording.store(true);
    emitRecordingChanged();
    emit logMessage(QStringLiteral("INFO"),
                    QStringLiteral("Recording clip from %1. Stop when you have the sound you want.")
                        .arg(recordingDisplayName()));

    m_thread = std::thread(&ClipFrequencyAnalyzer::captureThreadMain, this, processId);
}

void ClipFrequencyAnalyzer::stopAndAnalyze()
{
    if (!m_recording.load()) {
        emit logMessage(QStringLiteral("WARN"), QStringLiteral("No clip is being recorded"));
        return;
    }
    m_stopRequested.store(true);
}

void ClipFrequencyAnalyzer::captureThreadMain(unsigned long processId)
{
    AudioThreadUtils::enableFlushToZero();

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comInitializedOnThread = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_captureError = QStringLiteral("Could not initialize audio for clip capture");
        }
        QMetaObject::invokeMethod(this, &ClipFrequencyAnalyzer::onCaptureFinished, Qt::QueuedConnection);
        return;
    }

    ProcessLoopbackCapture capture;
    QString errorMessage;
    if (!capture.open(processId, 0.f, &errorMessage)) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_captureError = errorMessage.isEmpty()
                                 ? QStringLiteral("Could not capture this app")
                                 : errorMessage;
        }
        if (comInitializedOnThread) {
            CoUninitialize();
        }
        QMetaObject::invokeMethod(this, &ClipFrequencyAnalyzer::onCaptureFinished, Qt::QueuedConnection);
        return;
    }

    const float sampleRate = capture.sampleRate();
    const int channelCount = std::max(1, capture.channelCount());
    const int maxFrames = static_cast<int>(sampleRate * kMaxSeconds + 0.5f);
    std::vector<float> chunk(static_cast<size_t>(kFrameChunk * channelCount), 0.f);
    std::vector<float> mono;
    mono.reserve(static_cast<size_t>(maxFrames));

    int processCheckCounter = 0;
    constexpr int kProcessCheckInterval = 100;

    while (!m_stopRequested.load()) {
        if (++processCheckCounter >= kProcessCheckInterval) {
            processCheckCounter = 0;
            if (!ProcessLoopbackCapture::isProcessRunning(processId)) {
                errorMessage = QStringLiteral("App exited while recording");
                break;
            }
        }

        int framesRead = 0;
        if (!capture.read(chunk.data(), kFrameChunk, &framesRead, &errorMessage)) {
            break;
        }
        if (framesRead <= 0) {
            Sleep(1);
            continue;
        }

        const int room = maxFrames - static_cast<int>(mono.size());
        const int take = std::min(framesRead, room);
        for (int frame = 0; frame < take; ++frame) {
            mono.push_back(mixdownSample(chunk.data(), frame, channelCount));
        }
        if (static_cast<int>(mono.size()) >= maxFrames) {
            break;
        }
    }

    capture.close();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mono = std::move(mono);
        m_sampleRate = sampleRate;
        if (!errorMessage.isEmpty()) {
            m_captureError = errorMessage;
        }
    }

    if (comInitializedOnThread) {
        CoUninitialize();
    }

    QMetaObject::invokeMethod(this, &ClipFrequencyAnalyzer::onCaptureFinished, Qt::QueuedConnection);
}

void ClipFrequencyAnalyzer::onCaptureFinished()
{
    if (m_thread.joinable()) {
        m_thread.join();
    }

    QString error;
    QString name;
    bool empty = true;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        error = m_captureError;
        name = m_displayName;
        empty = m_mono.empty();
    }

    m_recording.store(false);
    emitRecordingChanged();

    if (!error.isEmpty() && empty) {
        emit logMessage(QStringLiteral("WARN"),
                        QStringLiteral("Clip capture failed for %1: %2").arg(name, error));
        return;
    }

    const QString result = analyzeClip();
    if (result.isEmpty()) {
        emit logMessage(QStringLiteral("WARN"),
                        QStringLiteral("Clip (%1): nothing loud enough to locate").arg(name));
        return;
    }

    emit logMessage(QStringLiteral("INFO"), result);
}

QString ClipFrequencyAnalyzer::analyzeClip() const
{
    std::vector<float> mono;
    float sampleRate = 48000.f;
    QString name;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        mono = m_mono;
        sampleRate = m_sampleRate;
        name = m_displayName;
    }

    if (mono.size() < static_cast<size_t>(kFftSize) || sampleRate < 1.f) {
        return {};
    }

    const int hop = kFftSize / 2;
    const int hopCount = 1 + static_cast<int>((static_cast<int>(mono.size()) - kFftSize) / hop);
    if (hopCount <= 0) {
        return {};
    }

    std::vector<float> hopLevels(static_cast<size_t>(hopCount), 0.f);
    float peakRms = 0.f;
    for (int hopIndex = 0; hopIndex < hopCount; ++hopIndex) {
        const float rms = hopRms(mono.data() + hopIndex * hop, kFftSize);
        hopLevels[static_cast<size_t>(hopIndex)] = rms;
        peakRms = std::max(peakRms, rms);
    }

    const float loudFloor = std::max(peakRms * kQuietRelative, 1.0e-5f);
    const int halfBins = kFftSize / 2;
    std::vector<float> avgMag(static_cast<size_t>(halfBins), 0.f);
    int loudHops = 0;

    std::vector<float> real(static_cast<size_t>(kFftSize), 0.f);
    std::vector<float> imag(static_cast<size_t>(kFftSize), 0.f);

    for (int hopIndex = 0; hopIndex < hopCount; ++hopIndex) {
        if (hopLevels[static_cast<size_t>(hopIndex)] < loudFloor) {
            continue;
        }

        const float *src = mono.data() + hopIndex * hop;
        std::fill(imag.begin(), imag.end(), 0.f);
        for (int i = 0; i < kFftSize; ++i) {
            const float window =
                0.5f * (1.f - std::cos(2.f * kPi * static_cast<float>(i) / static_cast<float>(kFftSize - 1)));
            real[static_cast<size_t>(i)] = src[i] * window;
        }

        SpectrumAnalyzer::fftRadix2(real, imag);

        for (int bin = 0; bin < halfBins; ++bin) {
            const float re = real[static_cast<size_t>(bin)];
            const float im = imag[static_cast<size_t>(bin)];
            avgMag[static_cast<size_t>(bin)] += std::sqrt(re * re + im * im);
        }
        ++loudHops;
    }

    if (loudHops <= 0) {
        return {};
    }

    const float invHops = 1.f / static_cast<float>(loudHops);
    for (float &mag : avgMag) {
        mag *= invHops;
    }

    const int minBin = std::max(1, static_cast<int>(kMinHz * static_cast<float>(kFftSize) / sampleRate));
    const int maxBin = std::min(halfBins - 1, static_cast<int>(kMaxHz * static_cast<float>(kFftSize) / sampleRate));
    if (maxBin <= minBin) {
        return {};
    }

    float maxMag = 0.f;
    for (int bin = minBin; bin <= maxBin; ++bin) {
        maxMag = std::max(maxMag, avgMag[static_cast<size_t>(bin)]);
    }
    if (maxMag < 1.0e-8f) {
        return {};
    }

    const float magFloor = maxMag * kPeakRelative;
    struct Peak {
        float hz = 0.f;
        float mag = 0.f;
    };
    std::vector<Peak> candidates;
    for (int bin = minBin + 1; bin < maxBin; ++bin) {
        const float mag = avgMag[static_cast<size_t>(bin)];
        if (mag < magFloor) {
            continue;
        }
        if (mag < avgMag[static_cast<size_t>(bin - 1)] || mag < avgMag[static_cast<size_t>(bin + 1)]) {
            continue;
        }
        candidates.push_back({binFrequency(bin, kFftSize, sampleRate), mag});
    }

    if (candidates.empty()) {
        int bestBin = minBin;
        for (int bin = minBin; bin <= maxBin; ++bin) {
            if (avgMag[static_cast<size_t>(bin)] > avgMag[static_cast<size_t>(bestBin)]) {
                bestBin = bin;
            }
        }
        candidates.push_back({binFrequency(bestBin, kFftSize, sampleRate), avgMag[static_cast<size_t>(bestBin)]});
    }

    std::sort(candidates.begin(), candidates.end(), [](const Peak &a, const Peak &b) { return a.mag > b.mag; });

    std::vector<float> peaksHz;
    for (const Peak &candidate : candidates) {
        bool close = false;
        for (float existing : peaksHz) {
            if (tooCloseInOctaves(existing, candidate.hz)) {
                close = true;
                break;
            }
        }
        if (close) {
            continue;
        }
        peaksHz.push_back(candidate.hz);
        if (static_cast<int>(peaksHz.size()) >= kMaxPeaks) {
            break;
        }
    }

    std::sort(peaksHz.begin(), peaksHz.end());

    QStringList parts;
    parts.reserve(static_cast<int>(peaksHz.size()));
    for (float hz : peaksHz) {
        parts.append(formatHz(hz));
    }

    return QStringLiteral("Clip (%1): mostly lives around %2").arg(name, parts.join(QStringLiteral(", ")));
}
