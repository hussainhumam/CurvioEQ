#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    void check();
    void startUpdate(const QUrl &installerUrl);
    void fetchChangelog();
    static QString currentVersion();
    static bool isRemoteNewer(const QString &remoteTag, const QString &localVersion);
    static bool isTrustedInstallerUrl(const QUrl &url);
    static bool launchInstaller(const QString &installerPath, QString *errorMessage = nullptr);
    static QString extractRelevantChangelog(const QString &markdown,
                                            const QString &sinceVersion,
                                            const QString &currentVersion);

signals:
    void updateAvailable(const QString &version, const QUrl &installerUrl);
    void upToDate();
    void checkFailed(const QString &error);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void installReady(const QString &installerPath);
    void updateFailed(const QString &error);
    void changelogReady(const QString &markdown);
    void changelogFailed(const QString &error);

private:
    enum class Job {
        None,
        Check,
        Download
    };

    void abortActive();
    void onFinished();
    void onChangelogFinished();
    void failDownload(const QString &error);
    void startChangelogRequest(const QUrl &url);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_reply = nullptr;
    QNetworkReply *m_changelogReply = nullptr;
    QFile *m_file = nullptr;
    Job m_job = Job::None;
    bool m_changelogTriedMain = false;
};
