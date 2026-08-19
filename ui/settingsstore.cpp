#include "settingsstore.h"

#include "apppaths.h"
#include "appconstants.h"
#include "audio/surroundprocessor.h"
#include "audio/dynamicrangesettings.h"
#include "eqcolorpalette.h"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {
constexpr auto kRunKey = AppConstants::kAppId;
constexpr auto kLegacyRunKey = "PerAppEQ";
constexpr int kDefaultSurroundLevel = 50;
}

QString SettingsStore::settingsFilePath()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.json"));
}

bool SettingsStore::load()
{
    m_settings = AppSettings{};

    const QString path = settingsFilePath();
    QFile file(path);
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(1);
    m_settings.startWithWindows = root.value(QStringLiteral("startWithWindows")).toBool(false);
    m_settings.setupCompleted = root.value(QStringLiteral("setupCompleted")).toBool(false);
    m_settings.muteRoutingSink = root.value(QStringLiteral("muteRoutingSink")).toBool(true);
    m_settings.routingSinkDeviceId = root.value(QStringLiteral("routingSinkDeviceId")).toString();
    m_settings.routingSinkDeviceName = root.value(QStringLiteral("routingSinkDeviceName")).toString();
    m_settings.eqOutputDeviceId = root.value(QStringLiteral("eqOutputDeviceId")).toString();
    m_settings.eqOutputDeviceName = root.value(QStringLiteral("eqOutputDeviceName")).toString();
    m_settings.surroundEnabled = root.value(QStringLiteral("surroundEnabled")).toBool(false);
    m_settings.hrtfPresetId = root.value(QStringLiteral("hrtfPresetId")).toInt(0);
    m_settings.hrtfStrength = std::clamp(root.value(QStringLiteral("hrtfStrength")).toInt(75), 0, 100);
    m_settings.dynamicsEnabled = root.value(QStringLiteral("dynamicsEnabled")).toBool(false);
    m_settings.dynamicsAmount =
        clampDynamicRangeAmount(root.value(QStringLiteral("dynamicsAmount")).toInt(DynamicRangeSettings::kAmountDefault));
    m_settings.dynamicsLoudnessAmount = clampLoudnessAmount(
        root.value(QStringLiteral("dynamicsLoudnessAmount")).toInt(DynamicRangeSettings::kLoudnessDefault));
    m_settings.spectrumEnabled = root.value(QStringLiteral("spectrumEnabled")).toBool(true);
    m_settings.keybindsEnabled = root.value(QStringLiteral("keybindsEnabled")).toBool(false);
    m_settings.eqToggleKeybind = root.value(QStringLiteral("eqToggleKeybind")).toString();
    m_settings.outputMuteKeybind = root.value(QStringLiteral("outputMuteKeybind")).toString();

    const QJsonArray colorKeybinds = root.value(QStringLiteral("eqColorKeybinds")).toArray();
    for (int i = 0; i < AppSettings::kEqColorKeybindCount; ++i) {
        if (i < colorKeybinds.size()) {
            m_settings.eqColorKeybinds[static_cast<size_t>(i)] = colorKeybinds.at(i).toString();
        }
    }

    const QJsonArray levels = root.value(QStringLiteral("surroundChannelLevels")).toArray();
    for (int i = 0; i < AppSettings::kSurroundChannelCount; ++i) {
        int level = i == SurroundProcessor::Lfe ? 0 : kDefaultSurroundLevel;
        if (i < levels.size()) {
            level = levels.at(i).toInt(level);
        }
        m_settings.surroundChannelLevels[static_cast<size_t>(i)] = std::clamp(level, 0, 100);
    }

    if (version < 3) {
        m_settings.hrtfStrength = 75;
        m_settings.surroundChannelLevels[static_cast<size_t>(SurroundProcessor::Lfe)] = 0;
    }

    return true;
}

bool SettingsStore::save() const
{
    const QString path = settingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    root.insert(QStringLiteral("version"), 6);
    root.insert(QStringLiteral("startWithWindows"), m_settings.startWithWindows);
    root.insert(QStringLiteral("setupCompleted"), m_settings.setupCompleted);
    root.insert(QStringLiteral("muteRoutingSink"), m_settings.muteRoutingSink);
    root.insert(QStringLiteral("routingSinkDeviceId"), m_settings.routingSinkDeviceId);
    root.insert(QStringLiteral("routingSinkDeviceName"), m_settings.routingSinkDeviceName);
    root.insert(QStringLiteral("eqOutputDeviceId"), m_settings.eqOutputDeviceId);
    root.insert(QStringLiteral("eqOutputDeviceName"), m_settings.eqOutputDeviceName);
    root.insert(QStringLiteral("surroundEnabled"), m_settings.surroundEnabled);
    root.insert(QStringLiteral("hrtfPresetId"), m_settings.hrtfPresetId);
    root.insert(QStringLiteral("hrtfStrength"), m_settings.hrtfStrength);
    root.insert(QStringLiteral("dynamicsEnabled"), m_settings.dynamicsEnabled);
    root.insert(QStringLiteral("dynamicsAmount"), m_settings.dynamicsAmount);
    root.insert(QStringLiteral("dynamicsLoudnessAmount"), m_settings.dynamicsLoudnessAmount);
    root.insert(QStringLiteral("spectrumEnabled"), m_settings.spectrumEnabled);
    root.insert(QStringLiteral("keybindsEnabled"), m_settings.keybindsEnabled);
    root.insert(QStringLiteral("eqToggleKeybind"), m_settings.eqToggleKeybind);
    root.insert(QStringLiteral("outputMuteKeybind"), m_settings.outputMuteKeybind);

    QJsonArray colorKeybinds;
    for (const QString &keybind : m_settings.eqColorKeybinds) {
        colorKeybinds.append(keybind);
    }
    root.insert(QStringLiteral("eqColorKeybinds"), colorKeybinds);

    QJsonArray levels;
    for (int level : m_settings.surroundChannelLevels) {
        levels.append(level);
    }
    root.insert(QStringLiteral("surroundChannelLevels"), levels);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool SettingsStore::applyStartWithWindows(bool enabled, QString *errorMessage)
{
    QSettings runKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                     QSettings::NativeFormat);

    if (enabled) {
        const QString exePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString command = QStringLiteral("\"%1\" --startup").arg(exePath);
        runKey.remove(QString::fromLatin1(kLegacyRunKey));
        runKey.setValue(QString::fromLatin1(kRunKey), command);
    } else {
        runKey.remove(QString::fromLatin1(kRunKey));
        runKey.remove(QString::fromLatin1(kLegacyRunKey));
    }

    if (runKey.status() != QSettings::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to update Windows startup registry");
        }
        return false;
    }
    return true;
}
