#pragma once

#include <QString>
#include <QStringList>

#include <vector>

enum class SoundAssetFormat {
    Wav,
    Ogg,
    Mp3,
    Flac,
    Wem,
    FmodBank,
    FmodFsb,
    UnrealPak,
    Unknown,
};

enum class SoundAssetStatus {
    Original,
    Modified,
    Unsupported,
    Pending,
    Error,
};

enum class SoundPatchSupport {
    Full,
    DecodeOnly,
    Unsupported,
};

struct SoundAssetEntry {
    QString absolutePath;
    QString relativePath;
    QString displayName;
    SoundAssetFormat format = SoundAssetFormat::Unknown;
    SoundAssetStatus status = SoundAssetStatus::Original;
    SoundPatchSupport patchSupport = SoundPatchSupport::Unsupported;
    float gainDb = 0.f;
    bool enabled = true;
    qint64 fileSize = 0;
    QString statusMessage;
};

struct GameIdentity {
    QString id;
    QString displayName;
    QString executablePath;
    QString scanRoot;
};

struct SoundModProfileEntry {
    QString relativePath;
    float gainDb = 0.f;
    bool enabled = true;
};

struct SoundModManifestEntry {
    QString relativePath;
    QString backupRelativePath;
    QString originalSha256;
    float appliedGainDb = 0.f;
};

struct SoundScanResult {
    std::vector<SoundAssetEntry> assets;
    QStringList folderPaths;
    int scannedFileCount = 0;
    int scannedFolderCount = 0;
};
