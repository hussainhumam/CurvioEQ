#include "formathandlers.h"

#include "externaltoollocator.h"
#include "wavautil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

SoundAssetFormat LooseAudioHandler::format() const
{
    return SoundAssetFormat::Wav;
}

bool LooseAudioHandler::matchesExtension(const QString &extension) const
{
    return extension.compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0;
}

SoundPatchSupport LooseAudioHandler::patchSupport() const
{
    return SoundPatchSupport::Full;
}

bool LooseAudioHandler::applyGain(const QString &sourcePath,
                                  const QString &stagingPath,
                                  float gainDb,
                                  QString *errorMessage) const
{
    WavAudioUtil::AudioBuffer buffer;
    if (!WavAudioUtil::load(sourcePath, &buffer, errorMessage)) {
        return false;
    }
    WavAudioUtil::applyGainDb(&buffer, gainDb);
    QDir().mkpath(QFileInfo(stagingPath).absolutePath());
    return WavAudioUtil::save(stagingPath, buffer, errorMessage);
}

SoundAssetFormat FfmpegLooseHandler::format() const
{
    return SoundAssetFormat::Ogg;
}

bool FfmpegLooseHandler::matchesExtension(const QString &extension) const
{
    return extension.compare(QStringLiteral("ogg"), Qt::CaseInsensitive) == 0
        || extension.compare(QStringLiteral("mp3"), Qt::CaseInsensitive) == 0
        || extension.compare(QStringLiteral("flac"), Qt::CaseInsensitive) == 0;
}

SoundPatchSupport FfmpegLooseHandler::patchSupport() const
{
    return ExternalToolLocator::ffmpegPath().isEmpty() ? SoundPatchSupport::Unsupported
                                                       : SoundPatchSupport::Full;
}

bool FfmpegLooseHandler::transcodeWithGain(const QString &sourcePath,
                                           const QString &stagingPath,
                                           float gainDb,
                                           const QString &outputExtension,
                                           QString *errorMessage) const
{
    const QString ffmpeg = ExternalToolLocator::ffmpegPath();
    if (ffmpeg.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("ffmpeg not found");
        }
        return false;
    }

    const QString tempWav = stagingPath + QStringLiteral(".tmp.wav");
    const QString tempOut = stagingPath + QStringLiteral(".tmp.") + outputExtension;
    const QString volumeFilter = QStringLiteral("volume=%1dB").arg(gainDb, 0, 'f', 2);

    QString stdErr;
    if (!ExternalToolLocator::runProcess(ffmpeg,
                                         {QStringLiteral("-y"),
                                          QStringLiteral("-i"),
                                          sourcePath,
                                          QStringLiteral("-af"),
                                          volumeFilter,
                                          tempWav},
                                         nullptr,
                                         &stdErr)) {
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("ffmpeg decode failed") : stdErr;
        }
        return false;
    }

    if (outputExtension.compare(QStringLiteral("wav"), Qt::CaseInsensitive) == 0) {
        QFile::remove(stagingPath);
        return QFile::rename(tempWav, stagingPath);
    }

    if (!ExternalToolLocator::runProcess(ffmpeg,
                                         {QStringLiteral("-y"),
                                          QStringLiteral("-i"),
                                          tempWav,
                                          tempOut},
                                         nullptr,
                                         &stdErr)) {
        QFile::remove(tempWav);
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("ffmpeg encode failed") : stdErr;
        }
        return false;
    }

    QFile::remove(tempWav);
    QFile::remove(stagingPath);
    const bool renamed = QFile::rename(tempOut, stagingPath);
    if (!renamed && errorMessage) {
        *errorMessage = QStringLiteral("Could not replace encoded file");
    }
    return renamed;
}

bool FfmpegLooseHandler::applyGain(const QString &sourcePath,
                                   const QString &stagingPath,
                                   float gainDb,
                                   QString *errorMessage) const
{
    const QString extension = QFileInfo(sourcePath).suffix();
    QDir().mkpath(QFileInfo(stagingPath).absolutePath());
    return transcodeWithGain(sourcePath, stagingPath, gainDb, extension, errorMessage);
}

ExternalContainerHandler::ExternalContainerHandler(SoundAssetFormat assetFormat, const QStringList &extensions)
    : m_format(assetFormat)
    , m_extensions(extensions)
{
}

SoundAssetFormat ExternalContainerHandler::format() const
{
    return m_format;
}

bool ExternalContainerHandler::matchesExtension(const QString &extension) const
{
    for (const QString &candidate : m_extensions) {
        if (extension.compare(candidate, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

SoundPatchSupport ExternalContainerHandler::patchSupport() const
{
    if (!ExternalToolLocator::vgmstreamPath().isEmpty() && !ExternalToolLocator::ffmpegPath().isEmpty()) {
        return SoundPatchSupport::Full;
    }
    if (!ExternalToolLocator::vgmstreamPath().isEmpty()) {
        return SoundPatchSupport::DecodeOnly;
    }
    return SoundPatchSupport::Unsupported;
}

bool ExternalContainerHandler::applyGain(const QString &sourcePath,
                                         const QString &stagingPath,
                                         float gainDb,
                                         QString *errorMessage) const
{
    const QString vgmstream = ExternalToolLocator::vgmstreamPath();
    const QString ffmpeg = ExternalToolLocator::ffmpegPath();
    if (vgmstream.isEmpty() || ffmpeg.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("vgmstream/ffmpeg tools required for this format");
        }
        return false;
    }

    const QString tempWav = stagingPath + QStringLiteral(".decode.wav");
    const QString tempOut = stagingPath + QStringLiteral(".encode") + QFileInfo(sourcePath).suffix();
    QString stdErr;

    if (!ExternalToolLocator::runProcess(vgmstream,
                                         {QStringLiteral("-o"),
                                          tempWav,
                                          sourcePath},
                                         nullptr,
                                         &stdErr)) {
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("vgmstream decode failed") : stdErr;
        }
        return false;
    }

    const QString volumeFilter = QStringLiteral("volume=%1dB").arg(gainDb, 0, 'f', 2);
    const QString tempAdjusted = stagingPath + QStringLiteral(".adjusted.wav");
    if (!ExternalToolLocator::runProcess(ffmpeg,
                                         {QStringLiteral("-y"),
                                          QStringLiteral("-i"),
                                          tempWav,
                                          QStringLiteral("-af"),
                                          volumeFilter,
                                          tempAdjusted},
                                         nullptr,
                                         &stdErr)) {
        QFile::remove(tempWav);
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("ffmpeg gain failed") : stdErr;
        }
        return false;
    }

    if (!ExternalToolLocator::runProcess(vgmstream,
                                         {QStringLiteral("-o"),
                                          tempOut,
                                          tempAdjusted},
                                         nullptr,
                                         &stdErr)) {
        QFile::remove(tempWav);
        QFile::remove(tempAdjusted);
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("vgmstream encode failed") : stdErr;
        }
        return false;
    }

    QFile::remove(tempWav);
    QFile::remove(tempAdjusted);
    QFile::remove(stagingPath);
    if (!QFile::rename(tempOut, stagingPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not replace patched container file");
        }
        return false;
    }
    return true;
}
