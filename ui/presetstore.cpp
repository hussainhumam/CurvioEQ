#include "presetstore.h"

#include "apppaths.h"
#include "audio/surroundprocessor.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace {
constexpr int kDefaultSurroundLevel = 50;

EqPreset makeBuiltIn(const QString &id, const QString &name,
                     const std::array<float, EqProcessor::kBandCount> &gainsDb)
{
    EqPreset preset;
    preset.id = id;
    preset.name = name;
    preset.eq.advanced = false;
    preset.eq.gainsDb = gainsDb;
    preset.hasEq = true;
    preset.isBuiltIn = true;
    return preset;
}

QString filterTypeToStringLocal(EqFilterType type)
{
    switch (type) {
    case EqFilterType::LowShelf:
        return QStringLiteral("lowshelf");
    case EqFilterType::HighShelf:
        return QStringLiteral("highshelf");
    case EqFilterType::Peaking:
    default:
        return QStringLiteral("peaking");
    }
}

bool filterTypeFromStringLocal(const QString &text, EqFilterType *type)
{
    if (!type) {
        return false;
    }
    const QString key = text.trimmed().toLower();
    if (key == QLatin1String("lowshelf") || key == QLatin1String("ls")) {
        *type = EqFilterType::LowShelf;
        return true;
    }
    if (key == QLatin1String("highshelf") || key == QLatin1String("hs")) {
        *type = EqFilterType::HighShelf;
        return true;
    }
    if (key == QLatin1String("peaking") || key == QLatin1String("pk") || key.isEmpty()) {
        *type = EqFilterType::Peaking;
        return true;
    }
    return false;
}

QJsonObject surroundToJson(const VirtualSurroundSettings &surround)
{
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), surround.enabled);
    object.insert(QStringLiteral("hrtfPresetId"), surround.presetId);
    object.insert(QStringLiteral("hrtfStrength"), surround.strength);
    QJsonArray levels;
    for (int level : surround.channelLevels) {
        levels.append(level);
    }
    object.insert(QStringLiteral("channelLevels"), levels);
    return object;
}

VirtualSurroundSettings surroundFromJson(const QJsonObject &object)
{
    VirtualSurroundSettings surround;
    surround.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    surround.presetId = object.value(QStringLiteral("hrtfPresetId")).toInt(0);
    surround.strength = std::clamp(object.value(QStringLiteral("hrtfStrength")).toInt(75), 0, 100);
    const QJsonArray levels = object.value(QStringLiteral("channelLevels")).toArray();
    for (int i = 0; i < SurroundProcessor::kChannelCount; ++i) {
        int level = i == SurroundProcessor::Lfe ? 0 : kDefaultSurroundLevel;
        if (i < levels.size()) {
            level = levels.at(i).toInt(level);
        }
        surround.channelLevels[static_cast<size_t>(i)] = std::clamp(level, 0, 100);
    }
    surround.channelLevels[static_cast<size_t>(SurroundProcessor::Lfe)] = 0;
    return surround;
}

QJsonObject dynamicsToJson(const DynamicRangeSettings &dynamics)
{
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), dynamics.enabled);
    object.insert(QStringLiteral("amount"), dynamics.amount);
    object.insert(QStringLiteral("loudnessAmount"), dynamics.loudnessAmount);
    object.insert(QStringLiteral("loudnessScale"), DynamicRangeSettings::kLoudnessScaleV3);
    return object;
}

DynamicRangeSettings dynamicsFromJson(const QJsonObject &object)
{
    DynamicRangeSettings dynamics;
    dynamics.enabled = object.value(QStringLiteral("enabled")).toBool(false);
    dynamics.amount = clampDynamicRangeAmount(
        object.value(QStringLiteral("amount")).toInt(DynamicRangeSettings::kAmountDefault));
    int loudnessAmount =
        object.value(QStringLiteral("loudnessAmount")).toInt(DynamicRangeSettings::kLoudnessDefault);
    const int loudnessScale = object.value(QStringLiteral("loudnessScale")).toInt(1);
    if (loudnessScale < DynamicRangeSettings::kLoudnessScaleV2) {
        loudnessAmount = migrateLegacyLoudnessAmount(loudnessAmount);
    } else if (loudnessScale < DynamicRangeSettings::kLoudnessScaleV3) {
        loudnessAmount = migrateV2LoudnessAmount(loudnessAmount);
    }
    dynamics.loudnessAmount = clampLoudnessAmount(loudnessAmount);
    return dynamics;
}

QJsonArray audioChainToJson(const AudioChainOrder &order)
{
    QJsonArray array;
    const AudioChainOrder chain = builtinsOnly(normalizeAudioChainOrder(order));
    for (int i = 0; i < chain.count; ++i) {
        array.append(QString::fromLatin1(audioChainStageId(chain.stages[static_cast<size_t>(i)])));
    }
    return array;
}

AudioChainOrder audioChainFromJson(const QJsonArray &array)
{
    AudioChainOrder loaded = defaultAudioChainOrder();
    if (array.isEmpty() || array.size() > kAudioChainBuiltinCount) {
        return loaded;
    }
    loaded.count = 0;
    for (int i = 0; i < array.size(); ++i) {
        AudioChainStage stage = AudioChainStage::Eq;
        const QByteArray id = array.at(i).toString().toUtf8();
        if (!audioChainStageFromId(id.constData(), &stage) || audioChainStageIsAddon(stage)) {
            return defaultAudioChainOrder();
        }
        loaded.stages[static_cast<size_t>(loaded.count++)] = stage;
    }
    return normalizeAudioChainOrder(loaded);
}

} // namespace

QJsonObject PresetStore::toJsonObject(const EqPreset &preset)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), preset.id);
    object.insert(QStringLiteral("name"), preset.name);
    object.insert(QStringLiteral("builtIn"), preset.isBuiltIn);

    if (preset.hasEq) {
        QJsonArray gains;
        for (float gain : preset.eq.gainsDb) {
            gains.append(gain);
        }

        QJsonArray filters;
        if (preset.eq.advanced) {
            for (int i = 0; i < preset.eq.filterCount; ++i) {
                const EqFilter &filter = preset.eq.filters[static_cast<size_t>(i)];
                QJsonObject filterObject;
                filterObject.insert(QStringLiteral("type"), filterTypeToStringLocal(filter.type));
                filterObject.insert(QStringLiteral("fc"), filter.freqHz);
                filterObject.insert(QStringLiteral("gain"), filter.gainDb);
                filterObject.insert(QStringLiteral("q"), filter.q);
                filters.append(filterObject);
            }
        }

        object.insert(QStringLiteral("mode"),
                      preset.eq.advanced ? QStringLiteral("advanced") : QStringLiteral("simple"));
        object.insert(QStringLiteral("gainsDb"), gains);
        object.insert(QStringLiteral("filters"), filters);
    }

    QJsonObject include;
    include.insert(QStringLiteral("eq"), preset.hasEq);
    include.insert(QStringLiteral("surround"), preset.hasSurround);
    include.insert(QStringLiteral("dynamics"), preset.hasDynamics);
    include.insert(QStringLiteral("audioChain"), preset.hasAudioChain);
    object.insert(QStringLiteral("include"), include);

    if (preset.hasSurround) {
        object.insert(QStringLiteral("surround"), surroundToJson(preset.surround));
    }
    if (preset.hasDynamics) {
        object.insert(QStringLiteral("dynamics"), dynamicsToJson(preset.dynamics));
    }
    if (preset.hasAudioChain) {
        object.insert(QStringLiteral("audioChainOrder"), audioChainToJson(preset.audioChainOrder));
    }
    return object;
}

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
    m_favoriteIds.clear();

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

    const QJsonObject root = doc.object();
    const QJsonArray presets = root.value(QStringLiteral("presets")).toArray();
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

    const QJsonArray favorites = root.value(QStringLiteral("favorites")).toArray();
    for (const QJsonValue &value : favorites) {
        const QString id = value.toString();
        if (id.isEmpty() || m_favoriteIds.contains(id)) {
            continue;
        }
        if (presetById(id).id.isEmpty()) {
            continue;
        }
        m_favoriteIds.push_back(id);
    }

    return true;
}

bool PresetStore::save() const
{
    const QString path = presetsFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray array;
    for (const EqPreset &preset : m_userPresets) {
        array.append(toJsonObject(preset));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 3);
    root.insert(QStringLiteral("presets"), array);
    QJsonArray favorites;
    for (const QString &id : m_favoriteIds) {
        favorites.append(id);
    }
    root.insert(QStringLiteral("favorites"), favorites);

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

    const QJsonObject include = object.value(QStringLiteral("include")).toObject();
    if (include.isEmpty()) {
        parsed.hasEq = true;
        parsed.hasSurround = false;
        parsed.hasDynamics = false;
        parsed.hasAudioChain = false;
    } else {
        parsed.hasEq = include.value(QStringLiteral("eq")).toBool(false);
        parsed.hasSurround = include.value(QStringLiteral("surround")).toBool(false);
        parsed.hasDynamics = include.value(QStringLiteral("dynamics")).toBool(false);
        parsed.hasAudioChain = include.value(QStringLiteral("audioChain")).toBool(false);
        if (!parsed.hasEq && !parsed.hasSurround && !parsed.hasDynamics && !parsed.hasAudioChain) {
            parsed.hasEq = true;
        }
    }

    if (parsed.hasEq) {
        const QJsonArray gains = object.value(QStringLiteral("gainsDb")).toArray();
        if (gains.size() != EqProcessor::kBandCount) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("Preset must contain exactly %1 gain values")
                                    .arg(EqProcessor::kBandCount);
            }
            return false;
        }

        parsed.eq.advanced = object.value(QStringLiteral("mode")).toString().compare(
                                 QStringLiteral("advanced"), Qt::CaseInsensitive)
                             == 0;

        for (int i = 0; i < EqProcessor::kBandCount; ++i) {
            parsed.eq.gainsDb[static_cast<size_t>(i)] = static_cast<float>(gains.at(i).toDouble());
        }

        const QJsonArray filters = object.value(QStringLiteral("filters")).toArray();
        parsed.eq.clearFilters();
        for (const QJsonValue &value : filters) {
            if (parsed.eq.filterCount >= EqState::kMaxParametricFilters) {
                break;
            }
            const QJsonObject filterObject = value.toObject();
            EqFilter filter;
            if (!filterTypeFromStringLocal(filterObject.value(QStringLiteral("type")).toString(), &filter.type)) {
                continue;
            }
            filter.freqHz = static_cast<float>(filterObject.value(QStringLiteral("fc")).toDouble(1000.0));
            filter.gainDb = static_cast<float>(filterObject.value(QStringLiteral("gain")).toDouble(0.0));
            filter.q = static_cast<float>(filterObject.value(QStringLiteral("q")).toDouble(1.41));
            parsed.eq.filters[static_cast<size_t>(parsed.eq.filterCount++)] = filter;
        }

        if (parsed.eq.filterCount > 0) {
            parsed.eq.advanced = true;
        }

        if (parsed.eq.advanced && parsed.eq.filterCount == 0) {
            parsed.eq = EqResponse::simpleToAdvanced(parsed.eq.gainsDb);
        }
    }

    if (parsed.hasSurround) {
        parsed.surround = surroundFromJson(object.value(QStringLiteral("surround")).toObject());
    }
    if (parsed.hasDynamics) {
        parsed.dynamics = dynamicsFromJson(object.value(QStringLiteral("dynamics")).toObject());
    }
    if (parsed.hasAudioChain) {
        parsed.audioChainOrder = audioChainFromJson(object.value(QStringLiteral("audioChainOrder")).toArray());
    }

    if (parsed.id.isEmpty()) {
        parsed.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    *preset = parsed;
    return true;
}

bool PresetStore::addUserPreset(const QString &name,
                                const EqState &eqState,
                                EqPreset *createdPreset)
{
    EqPreset preset;
    preset.name = name;
    preset.eq = eqState;
    preset.hasEq = true;
    return addUserPreset(preset, createdPreset);
}

bool PresetStore::addUserPreset(const EqPreset &source, EqPreset *createdPreset)
{
    EqPreset preset = source;
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = makeUniqueName(preset.name, m_userPresets);
    preset.isBuiltIn = false;
    if (!preset.hasEq && !preset.hasSurround && !preset.hasDynamics && !preset.hasAudioChain) {
        preset.hasEq = true;
    }
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

QString PresetStore::includedSectionsLabel(const EqPreset &preset)
{
    QStringList parts;
    if (preset.hasEq) {
        parts.append(QStringLiteral("EQ"));
    }
    if (preset.hasSurround) {
        parts.append(QStringLiteral("HRTF"));
    }
    if (preset.hasDynamics) {
        parts.append(QStringLiteral("Dynamics"));
    }
    if (preset.hasAudioChain) {
        parts.append(QStringLiteral("Chain"));
    }
    return parts.join(QLatin1Char('+'));
}

bool PresetStore::removeUserPreset(const QString &id)
{
    for (int i = 0; i < m_userPresets.size(); ++i) {
        if (m_userPresets.at(i).id == id) {
            m_userPresets.removeAt(i);
            m_favoriteIds.removeAll(id);
            return save();
        }
    }
    return false;
}

bool PresetStore::isFavorite(const QString &id) const
{
    return !id.isEmpty() && m_favoriteIds.contains(id);
}

bool PresetStore::setFavorite(const QString &id, bool favorite)
{
    if (presetById(id).id.isEmpty()) {
        return false;
    }

    m_favoriteIds.removeAll(id);
    if (favorite) {
        m_favoriteIds.prepend(id);
    }
    return save();
}

QStringList PresetStore::favoriteIds() const
{
    return m_favoriteIds;
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

    QJsonObject root;
    root.insert(QStringLiteral("version"), 3);
    root.insert(QStringLiteral("presets"), QJsonArray{toJsonObject(preset)});

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
