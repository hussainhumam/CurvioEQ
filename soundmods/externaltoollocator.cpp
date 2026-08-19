#include "externaltoollocator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

QString ExternalToolLocator::ffmpegPath()
{
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/ffmpeg.exe"));
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }

    const QString pathExe = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (!pathExe.isEmpty()) {
        return pathExe;
    }
    return {};
}

QString ExternalToolLocator::vgmstreamPath()
{
    const QString bundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/vgmstream/vgmstream-cli.exe"));
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }

    const QString altBundled =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/vgmstream-cli.exe"));
    if (QFileInfo::exists(altBundled)) {
        return altBundled;
    }

    const QString pathExe = QStandardPaths::findExecutable(QStringLiteral("vgmstream-cli"));
    if (!pathExe.isEmpty()) {
        return pathExe;
    }
    return {};
}

bool ExternalToolLocator::runProcess(const QString &program,
                                     const QStringList &arguments,
                                     QString *stdOut,
                                     QString *stdErr,
                                     int timeoutMs)
{
    if (program.isEmpty()) {
        return false;
    }

    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        return false;
    }
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        return false;
    }

    if (stdOut) {
        *stdOut = QString::fromUtf8(process.readAllStandardOutput());
    }
    if (stdErr) {
        *stdErr = QString::fromUtf8(process.readAllStandardError());
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
