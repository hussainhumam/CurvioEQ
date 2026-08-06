#include "presetstore.h"

#include "apppaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {

EqPreset makeBuiltIn(const QString &id, const QString &name,
                     const std::array<float, EqProcessor::kBandCount> &gainsDb)
{
    EqPreset preset;
    preset.id = id;
    preset.name = name;
    preset.gainsDb = gainsDb;
    preset.isBuiltIn = true;
    return preset;
}

} // namespace

PresetStore::PresetStore() = default;

QVector<EqPreset> PresetStore::defaultBuiltIns()
{
    return {
        makeBuiltIn(QStringLiteral("builtin-flat"), QStringLiteral("Flat"),
                    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}),
        makeBuiltIn(QStringLiteral("builtin-bass-boost"), QStringLiteral("Bass Boost"),
                    {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}),
        makeBuiltIn(QStringLiteral("builtin-treble-boost"), QStringLiteral("Treble Boost"),
                    {0, 0, 0, 0, 0, 0, 2, 4, 6, 5}),
        makeBuiltIn(QStringLiteral("builtin-vocal-boost"), QStringLiteral("Vocal Boost"),
                    {-2, -1, 0, 2, 5, 6, 4, 1, 0, -1}),
        makeBuiltIn(QStringLiteral("builtin-rock"), QStringLiteral("Rock"),
                    {4, 3, 1, -1, 0, 0, -1, 1, 3, 4}),
        makeBuiltIn(QStringLiteral("builtin-electronic"), QStringLiteral("Electronic"),
                    {5, 4, 2, 0, -1, 0, 1, 2, 3, 2}),
        makeBuiltIn(QStringLiteral("builtin-warm-smooth"), QStringLiteral("Warm & Smooth"),
                    {2, 2, 1, 0, 0, 0, -1, -2, -3, -2}),
        makeBuiltIn(QStringLiteral("builtin-speech-clarity"), QStringLiteral("Speech Clarity"),
                    {-4, -3, -2, 0, 3, 5, 4, 2, 0, -2}),
        makeBuiltIn(QStringLiteral("builtin-pop-balanced"), QStringLiteral("Pop / Balanced"),
                    {2, 1, 0, 1, 2, 2, 1, 0, 1, 2}),
        makeBuiltIn(QStringLiteral("builtin-fps-footsteps"), QStringLiteral("FPS Footsteps"),
                    {-3, -2, -1, 0, 2, 4, 5, 6, 3, 0}),
        makeBuiltIn(QStringLiteral("builtin-competitive-shooter"), QStringLiteral("Competitive Shooter"),
                    {-4, -3, -2, 0, 1, 3, 5, 7, 4, -1}),
        makeBuiltIn(QStringLiteral("builtin-battle-royale"), QStringLiteral("Battle Royale"),
                    {-3, -2, 0, 1, 3, 4, 5, 4, 2, 0}),
        makeBuiltIn(QStringLiteral("builtin-rpg-immersive"), QStringLiteral("RPG Immersive"),
                    {3, 2, 1, 0, 1, 2, 1, 0, 2, 3}),
        makeBuiltIn(QStringLiteral("builtin-racing-engine"), QStringLiteral("Racing Engine"),
                    {5, 4, 3, 2, 1, 0, -1, 0, 1, 0}),
    };
}

QString PresetStore::presetsFilePath()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("presets.json"));
}

bool PresetStore::load()
{
    m_userPresets.clear();

    const QString path = presetsFilePath();
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

    const QJsonArray presets = doc.object().value(QStringLiteral("presets")).toArray();
    for (const QJsonValue &value : presets) {
        EqPreset preset;
        QString errorMessage;
        if (!parsePresetObject(value.toObject(), &preset, &errorMessage)) {
            continue;
        }
        if (preset.isBuiltIn) {
            continue;
        }
        m_userPresets.push_back(preset);
    }

    return true;
}

bool PresetStore::save() const
{
    const QString path = presetsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray array;
    for (const EqPreset &preset : m_userPresets) {
        QJsonArray gains;
        for (float gain : preset.gainsDb) {
            gains.append(gain);
        }

        QJsonObject object;
        object.insert(QStringLiteral("id"), preset.id);
        object.insert(QStringLiteral("name"), preset.name);
        object.insert(QStringLiteral("gainsDb"), gains);
        object.insert(QStringLiteral("builtIn"), false);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("presets"), array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

QVector<EqPreset> PresetStore::builtInPresets() const
{
    return defaultBuiltIns();
}

QVector<EqPreset> PresetStore::userPresets() const
{
    return m_userPresets;
}

QVector<EqPreset> PresetStore::allPresets() const
{
    QVector<EqPreset> presets = defaultBuiltIns();
    presets += m_userPresets;
    return presets;
}

EqPreset PresetStore::presetById(const QString &id) const
{
    for (const EqPreset &preset : defaultBuiltIns()) {
        if (preset.id == id) {
            return preset;
        }
    }
    for (const EqPreset &preset : m_userPresets) {
        if (preset.id == id) {
            return preset;
        }
    }
    return {};
}

QString PresetStore::makeUniqueName(const QString &baseName, const QVector<EqPreset> &existing)
{
    QString candidate = baseName.trimmed();
    if (candidate.isEmpty()) {
        candidate = QStringLiteral("Preset");
    }

    auto nameExists = [&existing](const QString &name) {
        for (const EqPreset &preset : existing) {
            if (preset.name.compare(name, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        for (const EqPreset &preset : defaultBuiltIns()) {
            if (preset.name.compare(name, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };

    if (!nameExists(candidate)) {
        return candidate;
    }

    for (int suffix = 2; suffix < 1000; ++suffix) {
        const QString next = QStringLiteral("%1 (%2)").arg(baseName, QString::number(suffix));
        if (!nameExists(next)) {
            return next;
        }
    }

    return candidate + QUuid::createUuid().toString(QUuid::WithoutBraces).left(6);
}

bool PresetStore::parsePresetObject(const QJsonObject &object, EqPreset *preset, QString *errorMessage)
{
    if (!preset) {
        return false;
    }

    const QJsonArray gains = object.value(QStringLiteral("gainsDb")).toArray();
    if (gains.size() != EqProcessor::kBandCount) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Preset must contain exactly %1 gain values")
                                .arg(EqProcessor::kBandCount);
        }
        return false;
    }

    EqPreset parsed;
    parsed.id = object.value(QStringLiteral("id")).toString();
    parsed.name = object.value(QStringLiteral("name")).toString().trimmed();
    parsed.isBuiltIn = object.value(QStringLiteral("builtIn")).toBool(false);

    if (parsed.name.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Preset name is required");
        }
        return false;
    }

    for (int i = 0; i < EqProcessor::kBandCount; ++i) {
        parsed.gainsDb[static_cast<size_t>(i)] = static_cast<float>(gains.at(i).toDouble());
    }

    if (parsed.id.isEmpty()) {
        parsed.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    *preset = parsed;
    return true;
}

bool PresetStore::addUserPreset(const QString &name,
                                const std::array<float, EqProcessor::kBandCount> &gainsDb,
                                EqPreset *createdPreset)
{
    EqPreset preset;
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = makeUniqueName(name, m_userPresets);
    preset.gainsDb = gainsDb;
    preset.isBuiltIn = false;
    m_userPresets.push_back(preset);

    if (!save()) {
        m_userPresets.removeLast();
        return false;
    }

    if (createdPreset) {
        *createdPreset = preset;
    }
    return true;
}

bool PresetStore::removeUserPreset(const QString &id)
{
    for (int i = 0; i < m_userPresets.size(); ++i) {
        if (m_userPresets.at(i).id == id) {
            m_userPresets.removeAt(i);
            return save();
        }
    }
    return false;
}

bool PresetStore::importFromFile(const QString &path, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not open file: %1").arg(path);
        }
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Invalid preset file format");
        }
        return false;
    }

    const QJsonArray presets = doc.object().value(QStringLiteral("presets")).toArray();
    if (presets.isEmpty()) {
        EqPreset singlePreset;
        if (!parsePresetObject(doc.object(), &singlePreset, errorMessage)) {
            return false;
        }
        singlePreset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        singlePreset.name = makeUniqueName(singlePreset.name, m_userPresets);
        singlePreset.isBuiltIn = false;
        m_userPresets.push_back(singlePreset);
        return save();
    }

    for (const QJsonValue &value : presets) {
        EqPreset preset;
        if (!parsePresetObject(value.toObject(), &preset, errorMessage)) {
            return false;
        }
        preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        preset.name = makeUniqueName(preset.name, m_userPresets);
        preset.isBuiltIn = false;
        m_userPresets.push_back(preset);
    }
    return save();
}

bool PresetStore::exportToFile(const QString &id, const QString &path, QString *errorMessage) const
{
    const EqPreset preset = presetById(id);
    if (preset.id.isEmpty() || preset.isBuiltIn) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Preset not found or cannot export built-in presets");
        }
        return false;
    }

    QJsonArray gains;
    for (float gain : preset.gainsDb) {
        gains.append(gain);
    }

    QJsonObject object;
    object.insert(QStringLiteral("id"), preset.id);
    object.insert(QStringLiteral("name"), preset.name);
    object.insert(QStringLiteral("gainsDb"), gains);
    object.insert(QStringLiteral("builtIn"), false);

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("presets"), QJsonArray{object});

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not write file: %1").arg(path);
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
