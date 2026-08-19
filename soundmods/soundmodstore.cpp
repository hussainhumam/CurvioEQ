#include "soundmodstore.h"

#include "soundmodpaths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QJsonObject profileEntryToJson(const SoundModProfileEntry &entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("relativePath"), entry.relativePath);
    object.insert(QStringLiteral("gainDb"), entry.gainDb);
    object.insert(QStringLiteral("enabled"), entry.enabled);
    return object;
}

QJsonObject manifestEntryToJson(const SoundModManifestEntry &entry)
{
    QJsonObject object;
    object.insert(QStringLiteral("relativePath"), entry.relativePath);
    object.insert(QStringLiteral("backupRelativePath"), entry.backupRelativePath);
    object.insert(QStringLiteral("originalSha256"), entry.originalSha256);
    object.insert(QStringLiteral("appliedGainDb"), entry.appliedGainDb);
    return object;
}

} // namespace

bool SoundModStore::loadProfile(const QString &gameId, QVector<SoundModProfileEntry> *entries) const
{
    if (!entries) {
        return false;
    }
    entries->clear();

    QFile file(SoundModPaths::profilePath(gameId));
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

    const QJsonArray assets = doc.object().value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject object = value.toObject();
        SoundModProfileEntry entry;
        entry.relativePath = object.value(QStringLiteral("relativePath")).toString();
        entry.gainDb = static_cast<float>(object.value(QStringLiteral("gainDb")).toDouble(0.0));
        entry.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        if (!entry.relativePath.isEmpty()) {
            entries->push_back(entry);
        }
    }
    return true;
}

bool SoundModStore::saveProfile(const QString &gameId, const QVector<SoundModProfileEntry> &entries) const
{
    QDir().mkpath(SoundModPaths::gameRoot(gameId));

    QJsonArray assets;
    for (const SoundModProfileEntry &entry : entries) {
        assets.append(profileEntryToJson(entry));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("assets"), assets);

    QFile file(SoundModPaths::profilePath(gameId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool SoundModStore::loadManifest(const QString &gameId, QVector<SoundModManifestEntry> *entries) const
{
    if (!entries) {
        return false;
    }
    entries->clear();

    QFile file(SoundModPaths::manifestPath(gameId));
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

    const QJsonArray assets = doc.object().value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject object = value.toObject();
        SoundModManifestEntry entry;
        entry.relativePath = object.value(QStringLiteral("relativePath")).toString();
        entry.backupRelativePath = object.value(QStringLiteral("backupRelativePath")).toString();
        entry.originalSha256 = object.value(QStringLiteral("originalSha256")).toString();
        entry.appliedGainDb = static_cast<float>(object.value(QStringLiteral("appliedGainDb")).toDouble(0.0));
        if (!entry.relativePath.isEmpty()) {
            entries->push_back(entry);
        }
    }
    return true;
}

bool SoundModStore::saveManifest(const QString &gameId, const QVector<SoundModManifestEntry> &entries) const
{
    QDir().mkpath(SoundModPaths::gameRoot(gameId));

    QJsonArray assets;
    for (const SoundModManifestEntry &entry : entries) {
        assets.append(manifestEntryToJson(entry));
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("assets"), assets);

    QFile file(SoundModPaths::manifestPath(gameId));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

float SoundModStore::gainForRelativePath(const QString &gameId, const QString &relativePath) const
{
    QVector<SoundModProfileEntry> entries;
    if (!loadProfile(gameId, &entries)) {
        return 0.f;
    }
    for (const SoundModProfileEntry &entry : entries) {
        if (entry.relativePath == relativePath) {
            return entry.enabled ? entry.gainDb : 0.f;
        }
    }
    return 0.f;
}
