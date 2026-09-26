#pragma once

#include <array>
#include <QString>
#include <QStringList>

#include "ui/eqcolorpalette.h"
#include "ui/appconstants.h"
#include "audio/audiochainorder.h"
#include "audio/engineiosettings.h"
#include "audio/virtualsurroundsettings.h"
#include "audio/dynamicrangesettings.h"

struct AppSettings {
    static constexpr int kSurroundChannelCount = 8;
    static constexpr int kEqColorKeybindCount = EqColorPalette::kPresetColorCount;

    bool startWithWindows = false;
    bool setupCompleted = false;
    bool muteRoutingSink = true;
    QString routingSinkDeviceId;
    QString routingSinkDeviceName;
    QString eqOutputDeviceId;
    QString eqOutputDeviceName;
    bool surroundEnabled = false;
    int hrtfPresetId = 0;
    int hrtfStrength = 75;
    bool dynamicsEnabled = false;
    int dynamicsAmount = 35;
    int dynamicsLoudnessAmount = 0;
    AudioChainOrder audioChainOrder{};
    bool spectrumEnabled = true;
    float spectrumLimiterDb = 0.f;
    bool eqUiModeAdvanced = false;
    bool keybindsEnabled = false;
    QString eqToggleKeybind;
    QString outputMuteKeybind;
    std::array<QString, kEqColorKeybindCount> eqColorKeybinds{};
    std::array<int, kSurroundChannelCount> surroundChannelLevels = defaultVirtualSurroundChannelLevels();
    QString lastShownChangelogVersion;
    QStringList vst3ExtraFolders;
    int sampleRate = AppConstants::kDefaultSampleRate;
    int bufferFrames = AppConstants::kDefaultBufferFrames;
    ProcessingPrecision processingPrecision = ProcessingPrecision::Float32;
    ResampleQuality resampleQuality = ResampleQuality::Balanced;
    ChannelLayout channelLayout = ChannelLayout::Auto;
    OutputFormat outputFormat = OutputFormat::Auto;
    bool driftCorrection = false;
    bool safetyBufferAuto = true;
    int safetyBufferFrames = 0;
    ThreadPriority threadPriority = ThreadPriority::RealtimeAudio;
    bool cpuAffinityAuto = true;
    int cpuAffinityCore = 0;
    ShareMode shareMode = ShareMode::PreferShared;
    bool autoRecovery = true;

    EngineIoSettings engineIo() const
    {
        EngineIoSettings io;
        io.sampleRate = AppConstants::clampSampleRate(sampleRate);
        io.bufferFrames = AppConstants::clampBufferFrames(bufferFrames);
        io.precision = processingPrecision;
        io.resampleQuality = resampleQuality;
        io.channelLayout = channelLayout;
        io.outputFormat = outputFormat;
        io.driftCorrection = driftCorrection;
        io.safetyBufferAuto = safetyBufferAuto;
        io.safetyBufferFrames = safetyBufferFrames;
        io.threadPriority = threadPriority;
        io.cpuAffinityAuto = cpuAffinityAuto;
        io.cpuAffinityCore = cpuAffinityCore;
        io.shareMode = shareMode;
        io.autoRecovery = autoRecovery;
        return io;
    }
};

class SettingsStore
{
public:
    bool load();
    bool save() const;

    AppSettings settings() const { return m_settings; }
    void setSettings(const AppSettings &settings) { m_settings = settings; }

    static QString settingsFilePath();
    static bool applyStartWithWindows(bool enabled, QString *errorMessage = nullptr);

private:
    AppSettings m_settings;
};
