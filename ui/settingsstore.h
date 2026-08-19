#pragma once

#include <array>
#include <QString>

#include "ui/eqcolorpalette.h"
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
    bool spectrumEnabled = true;
    bool keybindsEnabled = false;
    QString eqToggleKeybind;
    QString outputMuteKeybind;
    std::array<QString, kEqColorKeybindCount> eqColorKeybinds{};
    std::array<int, kSurroundChannelCount> surroundChannelLevels = defaultVirtualSurroundChannelLevels();
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
