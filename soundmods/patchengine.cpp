#include "patchengine.h"

#include "backupstore.h"
#include "formathandler.h"
#include "soundmodpaths.h"
#include "soundmodstore.h"
#include "wavautil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>

#include <cmath>

namespace {

QVector<SoundModProfileEntry> profileFromAssets(const QVector<SoundAssetEntry> &assets)
{
    QVector<SoundModProfileEntry> profile;
    profile.reserve(assets.size());
    for (const SoundAssetEntry &asset : assets) {
        if (std::fabs(asset.gainDb) < 0.01f && asset.status != SoundAssetStatus::Modified) {
            continue;
        }
        SoundModProfileEntry entry;
        entry.relativePath = asset.relativePath;
        entry.gainDb = asset.gainDb;
        entry.enabled = asset.enabled;
        profile.push_back(entry);
    }
    return profile;
}

} // namespace

PatchApplyResult PatchEngine::apply(const GameIdentity &game,
                                    const QVector<SoundAssetEntry> &assets,
                                    const QString &scanRoot) const
{
    PatchApplyResult result;
    BackupStore backupStore;
    SoundModStore store;
    QVector<SoundModManifestEntry> manifest;

    QDir().mkpath(SoundModPaths::stagingRoot(game.id));

    for (const SoundAssetEntry &asset : assets) {
        if (!asset.enabled || std::fabs(asset.gainDb) < 0.01f) {
            continue;
        }
        if (asset.patchSupport == SoundPatchSupport::Unsupported) {
            result.failedFiles.append(asset.relativePath);
            continue;
        }

        const IFormatHandler *handler = FormatHandlerRegistry::instance().handlerForPath(asset.absolutePath);
        if (!handler) {
            result.failedFiles.append(asset.relativePath);
            continue;
        }

        const QString backupSource = backupStore.hasBackup(game.id, asset.relativePath)
            ? backupStore.backupPathFor(game.id, asset.relativePath)
            : asset.absolutePath;

        if (!backupStore.hasBackup(game.id, asset.relativePath)) {
            QString backupError;
            if (!backupStore.ensureBackup(game.id, asset.absolutePath, asset.relativePath, &backupError)) {
                result.failedFiles.append(asset.relativePath);
                continue;
            }
        }

        const QString stagingPath = QDir(SoundModPaths::stagingRoot(game.id)).filePath(asset.relativePath);
        QDir().mkpath(QFileInfo(stagingPath).absolutePath());

        QString patchError;
        if (!handler->applyGain(backupSource, stagingPath, asset.gainDb, &patchError)) {
            result.failedFiles.append(asset.relativePath);
            continue;
        }

        QFile::remove(asset.absolutePath);
        if (!QFile::rename(stagingPath, asset.absolutePath)) {
            if (!QFile::copy(stagingPath, asset.absolutePath)) {
                result.failedFiles.append(asset.relativePath);
                continue;
            }
            QFile::remove(stagingPath);
        }

        SoundModManifestEntry manifestEntry;
        manifestEntry.relativePath = asset.relativePath;
        manifestEntry.backupRelativePath = asset.relativePath;
        manifestEntry.originalSha256 = WavAudioUtil::sha256File(backupStore.backupPathFor(game.id, asset.relativePath));
        manifestEntry.appliedGainDb = asset.gainDb;
        manifest.push_back(manifestEntry);
        ++result.patchedCount;
    }

    if (!store.saveProfile(game.id, profileFromAssets(assets))) {
        result.message = QStringLiteral("Could not save sound mod profile");
        result.success = false;
        return result;
    }

    if (!store.saveManifest(game.id, manifest)) {
        result.message = QStringLiteral("Could not save sound mod manifest");
        result.success = false;
        return result;
    }

    result.success = result.failedFiles.isEmpty();
    if (result.success) {
        result.message = QStringLiteral("Patched %1 file(s)").arg(result.patchedCount);
    } else {
        result.message = QStringLiteral("Patched %1 file(s), %2 failed")
                             .arg(result.patchedCount)
                             .arg(result.failedFiles.size());
    }
    return result;
}

PatchApplyResult PatchEngine::restoreAll(const GameIdentity &game,
                                        const QVector<SoundAssetEntry> &assets,
                                        const QString &scanRoot) const
{
    QStringList relativePaths;
    for (const SoundAssetEntry &asset : assets) {
        relativePaths.append(asset.relativePath);
    }
    return restoreSelected(game, assets, scanRoot, relativePaths);
}

PatchApplyResult PatchEngine::restoreSelected(const GameIdentity &game,
                                              const QVector<SoundAssetEntry> &assets,
                                              const QString &scanRoot,
                                              const QStringList &relativePaths) const
{
    PatchApplyResult result;
    BackupStore backupStore;
    SoundModStore store;

    QHash<QString, SoundAssetEntry> assetByRelativePath;
    for (const SoundAssetEntry &asset : assets) {
        assetByRelativePath.insert(asset.relativePath, asset);
    }

    for (const QString &relativePath : relativePaths) {
        const QString targetPath = assetByRelativePath.contains(relativePath)
            ? assetByRelativePath.value(relativePath).absolutePath
            : QDir(scanRoot).filePath(relativePath);

        QString restoreError;
        if (!backupStore.hasBackup(game.id, relativePath)) {
            continue;
        }
        if (!backupStore.restoreFile(game.id, relativePath, targetPath, &restoreError)) {
            result.failedFiles.append(relativePath);
            continue;
        }
        ++result.restoredCount;
    }

    QVector<SoundModProfileEntry> profile;
    store.loadProfile(game.id, &profile);
    QVector<SoundModProfileEntry> remaining;
    for (const SoundModProfileEntry &entry : profile) {
        if (!relativePaths.contains(entry.relativePath)) {
            remaining.push_back(entry);
        }
    }
    store.saveProfile(game.id, remaining);

    QVector<SoundModManifestEntry> manifest;
    store.loadManifest(game.id, &manifest);
    QVector<SoundModManifestEntry> manifestRemaining;
    for (const SoundModManifestEntry &entry : manifest) {
        if (!relativePaths.contains(entry.relativePath)) {
            manifestRemaining.push_back(entry);
        }
    }
    store.saveManifest(game.id, manifestRemaining);

    result.success = result.failedFiles.isEmpty();
    result.message = QStringLiteral("Restored %1 file(s)").arg(result.restoredCount);
    return result;
}
