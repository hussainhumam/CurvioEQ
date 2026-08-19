#include "assetscanner.h"

#include "formathandler.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

namespace {

QString normalizeRelativePath(const QString &path)
{
    return QDir::fromNativeSeparators(path);
}

} // namespace

bool AssetScanner::isSkippablePathComponent(const QString &name)
{
    static const QStringList skip = {
        QStringLiteral(".git"),
        QStringLiteral(".vs"),
        QStringLiteral(".vscode"),
        QStringLiteral("node_modules"),
        QStringLiteral("CurvioEQ"),
        QStringLiteral("Windows"),
        QStringLiteral("System32"),
        QStringLiteral("__pycache__"),
    };
    for (const QString &candidate : skip) {
        if (name.compare(candidate, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool AssetScanner::pathContainsSkippableComponent(const QString &relativePath)
{
    const QString normalized = normalizeRelativePath(relativePath);
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (isSkippablePathComponent(part)) {
            return true;
        }
    }
    return false;
}

QString AssetScanner::formatLabel(SoundAssetFormat format)
{
    switch (format) {
    case SoundAssetFormat::Wav:
        return QStringLiteral("WAV");
    case SoundAssetFormat::Ogg:
        return QStringLiteral("OGG");
    case SoundAssetFormat::Mp3:
        return QStringLiteral("MP3");
    case SoundAssetFormat::Flac:
        return QStringLiteral("FLAC");
    case SoundAssetFormat::Wem:
        return QStringLiteral("WEM");
    case SoundAssetFormat::FmodBank:
        return QStringLiteral("FMOD Bank");
    case SoundAssetFormat::FmodFsb:
        return QStringLiteral("FMOD FSB");
    case SoundAssetFormat::UnrealPak:
        return QStringLiteral("Unreal Pak");
    default:
        return QStringLiteral("Unknown");
    }
}

SoundScanResult AssetScanner::scan(const QString &scanRoot, ProgressCallback progress) const
{
    SoundScanResult result;
    if (scanRoot.isEmpty() || !QDir(scanRoot).exists()) {
        return result;
    }

    const QDir rootDir(scanRoot);
    QSet<QString> folderSet;
    folderSet.insert(QString());

    QDirIterator iterator(scanRoot,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable,
                          QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);

    while (iterator.hasNext()) {
        const QString absolutePath = iterator.next();
        const QFileInfo info(absolutePath);
        const QString relativePath = normalizeRelativePath(rootDir.relativeFilePath(absolutePath));
        if (relativePath.isEmpty() || pathContainsSkippableComponent(relativePath)) {
            continue;
        }

        if (info.isDir()) {
            folderSet.insert(relativePath);
            ++result.scannedFolderCount;
            continue;
        }

        if (!info.isFile()) {
            continue;
        }

        ++result.scannedFileCount;

        const IFormatHandler *handler = FormatHandlerRegistry::instance().handlerForPath(absolutePath);
        if (!handler) {
            continue;
        }

        SoundAssetEntry entry;
        entry.absolutePath = normalizeRelativePath(absolutePath);
        entry.relativePath = relativePath;
        entry.displayName = info.fileName();
        entry.format = handler->format();
        entry.patchSupport = handler->patchSupport();
        entry.fileSize = info.size();
        entry.statusMessage = formatLabel(entry.format);
        if (entry.patchSupport == SoundPatchSupport::Unsupported) {
            entry.status = SoundAssetStatus::Unsupported;
            entry.statusMessage += QStringLiteral(" (tools required)");
        } else if (entry.patchSupport == SoundPatchSupport::DecodeOnly) {
            entry.status = SoundAssetStatus::Unsupported;
            entry.statusMessage += QStringLiteral(" (decode only)");
        } else {
            entry.status = SoundAssetStatus::Original;
        }

        result.assets.push_back(entry);
        if (progress) {
            progress(static_cast<int>(result.assets.size()));
        }
    }

    result.folderPaths = folderSet.values();
    result.folderPaths.sort(Qt::CaseInsensitive);
    return result;
}
