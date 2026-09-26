#include "settingsstore.h"

#include "apppaths.h"
#include "appconstants.h"
#include "audio/surroundprocessor.h"
#include "audio/dynamicrangesettings.h"
#include "audio/audiochainorder.h"
#include "audio/engineiosettings.h"
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

QString enumKey(ProcessingPrecision value)
{
    return value == ProcessingPrecision::Float64 ? QStringLiteral("float64") : QStringLiteral("float32");
}

QString enumKey(ResampleQuality value)
{
    if (value == ResampleQuality::Fast) {
        return QStringLiteral("fast");
    }
    if (value == ResampleQuality::Maximum) {
        return QStringLiteral("maximum");
    }
    return QStringLiteral("balanced");
}

QString enumKey(ChannelLayout value)
{
    if (value == ChannelLayout::Stereo) {
        return QStringLiteral("stereo");
    }
    if (value == ChannelLayout::Surround51) {
        return QStringLiteral("surround51");
    }
    if (value == ChannelLayout::Surround71) {
        return QStringLiteral("surround71");
    }
    return QStringLiteral("auto");
}

QString enumKey(OutputFormat value)
{
    if (value == OutputFormat::Float32) {
        return QStringLiteral("float32");
    }
    if (value == OutputFormat::Pcm16) {
        return QStringLiteral("pcm16");
    }
    return QStringLiteral("auto");
}

QString enumKey(ThreadPriority value)
{
    return value == ThreadPriority::Normal ? QStringLiteral("normal") : QStringLiteral("realtime");
}

QString enumKey(ShareMode value)
{
    return value == ShareMode::Exclusive ? QStringLiteral("exclusive") : QStringLiteral("shared");
}

QString enumKey(BufferSizePreset value)
{
    switch (value) {
    case BufferSizePreset::Balanced:
        return QStringLiteral("balanced");
    case BufferSizePreset::Safe:
        return QStringLiteral("safe");
    case BufferSizePreset::Custom:
        return QStringLiteral("custom");
    case BufferSizePreset::Low:
    default:
        return QStringLiteral("low");
    }
}

ProcessingPrecision parsePrecision(const QString &value)
{
    return value.compare(QStringLiteral("float64"), Qt::CaseInsensitive) == 0
               || value.compare(QStringLiteral("64"), Qt::CaseInsensitive) == 0
           ? ProcessingPrecision::Float64
           : ProcessingPrecision::Float32;
}

ResampleQuality parseResampleQuality(const QString &value)
{
    if (value.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("linear"), Qt::CaseInsensitive) == 0) {
        return ResampleQuality::Fast;
    }
    if (value.compare(QStringLiteral("maximum"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("sinc"), Qt::CaseInsensitive) == 0) {
        return ResampleQuality::Maximum;
    }
    return ResampleQuality::Balanced;
}

ChannelLayout parseChannelLayout(const QString &value)
{
    if (value.compare(QStringLiteral("stereo"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral("2")) {
        return ChannelLayout::Stereo;
    }
    if (value.compare(QStringLiteral("surround51"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("5.1"), Qt::CaseInsensitive) == 0) {
        return ChannelLayout::Surround51;
    }
    if (value.compare(QStringLiteral("surround71"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("7.1"), Qt::CaseInsensitive) == 0) {
        return ChannelLayout::Surround71;
    }
    return ChannelLayout::Auto;
}

OutputFormat parseOutputFormat(const QString &value)
{
    if (value.compare(QStringLiteral("float32"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("float"), Qt::CaseInsensitive) == 0) {
        return OutputFormat::Float32;
    }
    if (value.compare(QStringLiteral("pcm16"), Qt::CaseInsensitive) == 0
        || value.compare(QStringLiteral("int16"), Qt::CaseInsensitive) == 0) {
        return OutputFormat::Pcm16;
    }
    return OutputFormat::Auto;
}

ThreadPriority parseThreadPriority(const QString &value)
{
    return value.compare(QStringLiteral("normal"), Qt::CaseInsensitive) == 0
               ? ThreadPriority::Normal
               : ThreadPriority::RealtimeAudio;
}

ShareMode parseShareMode(const QString &value)
{
    return value.compare(QStringLiteral("exclusive"), Qt::CaseInsensitive) == 0
               ? ShareMode::Exclusive
               : ShareMode::PreferShared;
}

BufferSizePreset parseBufferPreset(const QString &value)
{
    if (value.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
        return BufferSizePreset::Low;
    }
    if (value.compare(QStringLiteral("balanced"), Qt::CaseInsensitive) == 0) {
        return BufferSizePreset::Balanced;
    }
    if (value.compare(QStringLiteral("safe"), Qt::CaseInsensitive) == 0) {
        return BufferSizePreset::Safe;
    }
    if (value.compare(QStringLiteral("custom"), Qt::CaseInsensitive) == 0) {
        return BufferSizePreset::Custom;
    }
    return BufferSizePreset::Low;
}

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
    {
        const QJsonValue setupValue = root.value(QStringLiteral("setupCompleted"));
        if (setupValue.isBool()) {
            m_settings.setupCompleted = setupValue.toBool();
        } else if (setupValue.isDouble()) {
            m_settings.setupCompleted = setupValue.toInt() != 0;
        } else {
            m_settings.setupCompleted = false;
        }
    }
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
    {
        int loudnessAmount =
            root.value(QStringLiteral("dynamicsLoudnessAmount")).toInt(DynamicRangeSettings::kLoudnessDefault);
        if (version < 9) {
            loudnessAmount = migrateLegacyLoudnessAmount(loudnessAmount);
        } else if (version < 10) {
            loudnessAmount = migrateV2LoudnessAmount(loudnessAmount);
        }
        m_settings.dynamicsLoudnessAmount = clampLoudnessAmount(loudnessAmount);
    }
    {
        AudioChainOrder loaded = defaultAudioChainOrder();
        const QJsonArray chain = root.value(QStringLiteral("audioChainOrder")).toArray();
        if (!chain.isEmpty() && chain.size() <= kAudioChainMaxStages) {
            bool ok = true;
            loaded.count = 0;
            for (int i = 0; i < chain.size(); ++i) {
                AudioChainStage stage = AudioChainStage::Eq;
                const QByteArray id = chain.at(i).toString().toUtf8();
                if (!audioChainStageFromId(id.constData(), &stage) || audioChainStageIsAddon(stage)) {
                    ok = false;
                    break;
                }
                loaded.stages[static_cast<size_t>(loaded.count++)] = stage;
            }
            if (ok) {
                m_settings.audioChainOrder = builtinsOnly(normalizeAudioChainOrder(loaded));
            }
        }
    }
    m_settings.spectrumEnabled = root.value(QStringLiteral("spectrumEnabled")).toBool(true);
    m_settings.spectrumLimiterDb = std::clamp(
        static_cast<float>(root.value(QStringLiteral("spectrumLimiterDb")).toDouble(0.0)),
        AppConstants::kSpectrumLimiterMinDb,
        AppConstants::kSpectrumLimiterMaxDb);
    m_settings.eqUiModeAdvanced = root.value(QStringLiteral("eqUiModeAdvanced")).toBool(false);
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

    m_settings.lastShownChangelogVersion = root.value(QStringLiteral("lastShownChangelogVersion")).toString();
    {
        const QJsonArray folders = root.value(QStringLiteral("vst3ExtraFolders")).toArray();
        for (const QJsonValue &value : folders) {
            const QString folder = value.toString().trimmed();
            if (!folder.isEmpty()) {
                m_settings.vst3ExtraFolders.append(folder);
            }
        }
    }

    m_settings.sampleRate = AppConstants::clampSampleRate(
        root.value(QStringLiteral("sampleRate")).toInt(AppConstants::kDefaultSampleRate));
    {
        const int loadedFrames = root.value(QStringLiteral("bufferFrames"))
                                     .toInt(AppConstants::kDefaultBufferFrames);
        if (version >= 11 && root.contains(QStringLiteral("bufferPreset"))) {
            const BufferSizePreset preset =
                parseBufferPreset(root.value(QStringLiteral("bufferPreset")).toString());
            m_settings.bufferFrames = EngineIoSettings::framesForBufferPreset(preset, loadedFrames);
        } else {
            m_settings.bufferFrames = AppConstants::clampBufferFrames(loadedFrames);
        }
    }
    if (m_settings.bufferFrames <= 0) {
        m_settings.bufferFrames = AppConstants::kDefaultBufferFrames;
    }
    m_settings.processingPrecision =
        parsePrecision(root.value(QStringLiteral("processingPrecision")).toString());
    m_settings.resampleQuality =
        parseResampleQuality(root.value(QStringLiteral("resampleQuality")).toString());
    m_settings.channelLayout = parseChannelLayout(root.value(QStringLiteral("channelLayout")).toString());
    m_settings.outputFormat = parseOutputFormat(root.value(QStringLiteral("outputFormat")).toString());
    m_settings.driftCorrection = root.value(QStringLiteral("driftCorrection")).toBool(false);
    m_settings.safetyBufferAuto = root.value(QStringLiteral("safetyBufferAuto")).toBool(true);
    m_settings.safetyBufferFrames =
        std::clamp(root.value(QStringLiteral("safetyBufferFrames")).toInt(0),
                   0,
                   EngineIoSettings::kMaxSafetyExtraFrames);
    m_settings.threadPriority = parseThreadPriority(root.value(QStringLiteral("threadPriority")).toString());
    m_settings.cpuAffinityAuto = root.value(QStringLiteral("cpuAffinityAuto")).toBool(true);
    m_settings.cpuAffinityCore = std::max(0, root.value(QStringLiteral("cpuAffinityCore")).toInt(0));
    m_settings.shareMode = parseShareMode(root.value(QStringLiteral("shareMode")).toString());
    m_settings.autoRecovery = root.value(QStringLiteral("autoRecovery")).toBool(true);

    if (!m_settings.setupCompleted && !m_settings.routingSinkDeviceId.isEmpty()
        && !m_settings.eqOutputDeviceId.isEmpty()) {
        m_settings.setupCompleted = true;
    }

    return true;
}

bool SettingsStore::save() const
{
    const QString path = settingsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    root.insert(QStringLiteral("version"), 11);
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
    QJsonArray audioChainOrder;
    const AudioChainOrder chain = builtinsOnly(normalizeAudioChainOrder(m_settings.audioChainOrder));
    for (int i = 0; i < chain.count; ++i) {
        audioChainOrder.append(QString::fromLatin1(audioChainStageId(chain.stages[static_cast<size_t>(i)])));
    }
    root.insert(QStringLiteral("audioChainOrder"), audioChainOrder);
    root.insert(QStringLiteral("spectrumEnabled"), m_settings.spectrumEnabled);
    root.insert(QStringLiteral("spectrumLimiterDb"), static_cast<double>(m_settings.spectrumLimiterDb));
    root.insert(QStringLiteral("eqUiModeAdvanced"), m_settings.eqUiModeAdvanced);
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
    root.insert(QStringLiteral("lastShownChangelogVersion"), m_settings.lastShownChangelogVersion);
    QJsonArray vst3Folders;
    for (const QString &folder : m_settings.vst3ExtraFolders) {
        vst3Folders.append(folder);
    }
    root.insert(QStringLiteral("vst3ExtraFolders"), vst3Folders);
    root.insert(QStringLiteral("sampleRate"), m_settings.sampleRate);
    root.insert(QStringLiteral("bufferFrames"),
                AppConstants::clampBufferFrames(m_settings.bufferFrames));
    root.insert(QStringLiteral("bufferPreset"),
                enumKey(EngineIoSettings::bufferPresetFromFrames(m_settings.bufferFrames)));
    root.insert(QStringLiteral("processingPrecision"), enumKey(m_settings.processingPrecision));
    root.insert(QStringLiteral("resampleQuality"), enumKey(m_settings.resampleQuality));
    root.insert(QStringLiteral("channelLayout"), enumKey(m_settings.channelLayout));
    root.insert(QStringLiteral("outputFormat"), enumKey(m_settings.outputFormat));
    root.insert(QStringLiteral("driftCorrection"), m_settings.driftCorrection);
    root.insert(QStringLiteral("safetyBufferAuto"), m_settings.safetyBufferAuto);
    root.insert(QStringLiteral("safetyBufferFrames"), m_settings.safetyBufferFrames);
    root.insert(QStringLiteral("threadPriority"), enumKey(m_settings.threadPriority));
    root.insert(QStringLiteral("cpuAffinityAuto"), m_settings.cpuAffinityAuto);
    root.insert(QStringLiteral("cpuAffinityCore"), m_settings.cpuAffinityCore);
    root.insert(QStringLiteral("shareMode"), enumKey(m_settings.shareMode));
    root.insert(QStringLiteral("autoRecovery"), m_settings.autoRecovery);

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
