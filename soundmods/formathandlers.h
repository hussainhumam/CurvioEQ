#pragma once

#include "formathandler.h"

#include <QStringList>

class LooseAudioHandler : public IFormatHandler
{
public:
    SoundAssetFormat format() const override;
    bool matchesExtension(const QString &extension) const override;
    SoundPatchSupport patchSupport() const override;
    bool applyGain(const QString &sourcePath, const QString &stagingPath, float gainDb, QString *errorMessage) const override;
};

class FfmpegLooseHandler : public IFormatHandler
{
public:
    SoundAssetFormat format() const override;
    bool matchesExtension(const QString &extension) const override;
    SoundPatchSupport patchSupport() const override;
    bool applyGain(const QString &sourcePath, const QString &stagingPath, float gainDb, QString *errorMessage) const override;

private:
    bool transcodeWithGain(const QString &sourcePath,
                           const QString &stagingPath,
                           float gainDb,
                           const QString &outputExtension,
                           QString *errorMessage) const;
};

class ExternalContainerHandler : public IFormatHandler
{
public:
    explicit ExternalContainerHandler(SoundAssetFormat assetFormat, const QStringList &extensions);

    SoundAssetFormat format() const override;
    bool matchesExtension(const QString &extension) const override;
    SoundPatchSupport patchSupport() const override;
    bool applyGain(const QString &sourcePath, const QString &stagingPath, float gainDb, QString *errorMessage) const override;

private:
    SoundAssetFormat m_format;
    QStringList m_extensions;
};
