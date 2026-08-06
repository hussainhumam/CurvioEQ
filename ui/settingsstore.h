#pragma once

#include <array>
#include <QString>

struct AppSettings {
    static constexpr int kSurroundChannelCount = 8;

    bool startWithWindows = false;
    QString routingSinkDeviceId;
    QString routingSinkDeviceName;
    QString eqOutputDeviceId;
    QString eqOutputDeviceName;
    bool surroundEnabled = false;
    std::array<int, kSurroundChannelCount> surroundChannelLevels{50, 50, 50, 50, 50, 50, 50, 50};
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
