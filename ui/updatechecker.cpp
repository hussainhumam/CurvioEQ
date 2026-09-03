#include "updatechecker.h"

#include "ui/appconstants.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QVersionNumber>

#include <string>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

constexpr char kInstallerFileName[] = "CurvioEQ-Setup.exe";

QString stripVersionPrefix(QString version)
{
    version = version.trimmed();
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V'))) {
        version = version.mid(1);
    }
    return version;
}

QVersionNumber parseVersion(const QString &text)
{
    const QString stripped = stripVersionPrefix(text);
    const QRegularExpression re(QStringLiteral(R"(^(\d+)(?:\.(\d+))?(?:\.(\d+))?)"));
    const QRegularExpressionMatch match = re.match(stripped);
    if (!match.hasMatch()) {
        return {};
    }
    QList<int> segments;
    segments.append(match.captured(1).toInt());
    if (!match.captured(2).isEmpty()) {
        segments.append(match.captured(2).toInt());
    }
    if (!match.captured(3).isEmpty()) {
        segments.append(match.captured(3).toInt());
    }
    return QVersionNumber(segments);
}

QNetworkRequest makeGitHubRequest(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("%1/%2")
                          .arg(QString::fromLatin1(AppConstants::kAppId),
                               QString::fromLatin1(AppConstants::kAppVersion)));
    request.setRawHeader("Accept", "application/octet-stream");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString installerTempPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QString::fromLatin1(kInstallerFileName));
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

UpdateChecker::~UpdateChecker()
{
    if (m_changelogReply) {
        m_changelogReply->disconnect(this);
        m_changelogReply->abort();
        m_changelogReply->deleteLater();
        m_changelogReply = nullptr;
    }
    abortActive();
}

QString UpdateChecker::currentVersion()
{
    return QString::fromLatin1(AppConstants::kAppVersion);
}

bool UpdateChecker::isRemoteNewer(const QString &remoteTag, const QString &localVersion)
{
    const QVersionNumber remote = parseVersion(remoteTag);
    const QVersionNumber local = parseVersion(localVersion);
    if (remote.isNull() || local.isNull()) {
        return false;
    }
    return remote > local;
}

bool UpdateChecker::isTrustedInstallerUrl(const QUrl &url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        return false;
    }
    if (url.host().compare(QLatin1String("github.com"), Qt::CaseInsensitive) != 0) {
        return false;
    }
    const QString expectedPrefix =
        QStringLiteral("/%1/%2/releases/download/")
            .arg(QString::fromLatin1(AppConstants::kGitHubOwner),
                 QString::fromLatin1(AppConstants::kGitHubRepo));
    const QString path = url.path();
    return path.startsWith(expectedPrefix, Qt::CaseInsensitive)
        && path.endsWith(QStringLiteral("/CurvioEQ-Setup.exe"), Qt::CaseInsensitive);
}

void UpdateChecker::abortActive()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_file) {
        m_file->close();
        if (m_job == Job::Download) {
            m_file->remove();
        }
        delete m_file;
        m_file = nullptr;
    }
    m_job = Job::None;
}

void UpdateChecker::check()
{
    abortActive();
    m_job = Job::Check;

    const QUrl url(QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest")
                       .arg(QString::fromLatin1(AppConstants::kGitHubOwner),
                            QString::fromLatin1(AppConstants::kGitHubRepo)));
    QNetworkRequest request = makeGitHubRequest(url);
    request.setRawHeader("Accept", "application/vnd.github+json");

    m_reply = m_nam->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onFinished);
}

void UpdateChecker::startUpdate(const QUrl &installerUrl)
{
    if (!isTrustedInstallerUrl(installerUrl)) {
        emit updateFailed(QStringLiteral("The installer URL is not from the CurvioEQ GitHub releases."));
        return;
    }

    abortActive();
    m_job = Job::Download;

    const QString path = installerTempPath();
    m_file = new QFile(path, this);
    if (m_file->exists() && !m_file->remove()) {
        failDownload(QStringLiteral("Could not replace a previous installer download."));
        return;
    }
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failDownload(QStringLiteral("Could not write the installer to temp."));
        return;
    }

    m_reply = m_nam->get(makeGitHubRequest(installerUrl));
    connect(m_reply, &QNetworkReply::downloadProgress, this, &UpdateChecker::downloadProgress);
    connect(m_reply, &QNetworkReply::readyRead, this, [this]() {
        if (m_file && m_reply) {
            m_file->write(m_reply->readAll());
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onFinished);
}

void UpdateChecker::failDownload(const QString &error)
{
    abortActive();
    emit updateFailed(error);
}

bool UpdateChecker::launchInstaller(const QString &installerPath, QString *errorMessage)
{
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(installerPath);
    if (!QFile::exists(nativePath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The downloaded installer is missing.");
        }
        return false;
    }

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    const std::wstring file = nativePath.toStdWString();
    const std::wstring params = L"/SP- /SILENT /NORESTART /CLOSEAPPLICATIONS";
    info.lpFile = file.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The update was cancelled or the installer could not start.");
        }
        return false;
    }
    if (info.hProcess) {
        CloseHandle(info.hProcess);
    }
    return true;
#else
    if (errorMessage) {
        *errorMessage = QStringLiteral("Automatic install is only supported on Windows.");
    }
    return false;
#endif
}

void UpdateChecker::onFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    const Job job = m_job;
    if (!reply) {
        m_job = Job::None;
        emit checkFailed(QStringLiteral("Update check failed"));
        return;
    }
    reply->deleteLater();

    if (job == Job::Download) {
        if (m_file) {
            m_file->write(reply->readAll());
            m_file->flush();
            m_file->close();
        }
        if (reply->error() != QNetworkReply::NoError) {
            failDownload(reply->errorString());
            return;
        }
        const QString path = m_file ? m_file->fileName() : QString();
        delete m_file;
        m_file = nullptr;
        m_job = Job::None;
        if (path.isEmpty() || !QFile::exists(path)) {
            emit updateFailed(QStringLiteral("The installer download was empty."));
            return;
        }
        emit installReady(path);
        return;
    }

    m_job = Job::None;
    if (reply->error() != QNetworkReply::NoError) {
        emit checkFailed(reply->errorString());
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isObject()) {
        emit checkFailed(QStringLiteral("GitHub returned an invalid release payload"));
        return;
    }

    const QJsonObject root = doc.object();
    const QString tag = root.value(QStringLiteral("tag_name")).toString();
    if (tag.isEmpty()) {
        emit checkFailed(QStringLiteral("GitHub release is missing a tag"));
        return;
    }

    QUrl installerUrl;
    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString()
            == QLatin1String(kInstallerFileName)) {
            installerUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
            break;
        }
    }

    if (!isRemoteNewer(tag, currentVersion())) {
        emit upToDate();
        return;
    }
    if (!isTrustedInstallerUrl(installerUrl)) {
        emit checkFailed(QStringLiteral("Latest GitHub release does not include CurvioEQ-Setup.exe"));
        return;
    }

    emit updateAvailable(stripVersionPrefix(tag), installerUrl);
}

QString UpdateChecker::extractRelevantChangelog(const QString &markdown,
                                                const QString &sinceVersion,
                                                const QString &currentVersion)
{
    const QString normalized = QString(markdown).replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QStringList lines = normalized.split(QLatin1Char('\n'));
    const QRegularExpression headerRe(QStringLiteral(R"(^##\s*\[([^\]]+)\])"));

    QString result;
    QString sectionId;
    QStringList sectionBody;

    const auto flush = [&]() {
        if (sectionId.isEmpty()) {
            return;
        }
        bool include = false;
        if (sectionId.compare(QLatin1String("Released"), Qt::CaseInsensitive) == 0) {
            include = true;
        } else if (sinceVersion.isEmpty()) {
            include = sectionId == currentVersion;
        } else {
            include = isRemoteNewer(sectionId, sinceVersion) || sectionId == currentVersion;
        }
        if (include) {
            if (!result.isEmpty()) {
                result += QLatin1Char('\n');
            }
            result += QStringLiteral("## [%1]\n").arg(sectionId);
            result += sectionBody.join(QLatin1Char('\n')).trimmed();
            result += QLatin1Char('\n');
        }
        sectionBody.clear();
    };

    for (const QString &line : lines) {
        const QRegularExpressionMatch match = headerRe.match(line);
        if (match.hasMatch()) {
            flush();
            sectionId = match.captured(1).trimmed();
            continue;
        }
        if (!sectionId.isEmpty()) {
            sectionBody.append(line);
        }
    }
    flush();
    return result.trimmed();
}

void UpdateChecker::startChangelogRequest(const QUrl &url)
{
    if (m_changelogReply) {
        m_changelogReply->disconnect(this);
        m_changelogReply->abort();
        m_changelogReply->deleteLater();
        m_changelogReply = nullptr;
    }

    QNetworkRequest request = makeGitHubRequest(url);
    request.setRawHeader("Accept", "text/plain");
    m_changelogReply = m_nam->get(request);
    connect(m_changelogReply, &QNetworkReply::finished, this, &UpdateChecker::onChangelogFinished);
}

void UpdateChecker::fetchChangelog()
{
    m_changelogTriedMain = false;
    const QUrl tagged(QStringLiteral("https://raw.githubusercontent.com/%1/%2/v%3/CHANGELOG.md")
                          .arg(QString::fromLatin1(AppConstants::kGitHubOwner),
                               QString::fromLatin1(AppConstants::kGitHubRepo),
                               currentVersion()));
    startChangelogRequest(tagged);
}

void UpdateChecker::onChangelogFinished()
{
    QNetworkReply *reply = m_changelogReply;
    m_changelogReply = nullptr;
    if (!reply) {
        emit changelogFailed(QStringLiteral("Could not download the changelog."));
        return;
    }
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if ((reply->error() != QNetworkReply::NoError || status >= 400) && !m_changelogTriedMain) {
        m_changelogTriedMain = true;
        const QUrl mainUrl(QStringLiteral("https://raw.githubusercontent.com/%1/%2/main/CHANGELOG.md")
                               .arg(QString::fromLatin1(AppConstants::kGitHubOwner),
                                    QString::fromLatin1(AppConstants::kGitHubRepo)));
        startChangelogRequest(mainUrl);
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        emit changelogFailed(reply->errorString());
        return;
    }

    emit changelogReady(QString::fromUtf8(reply->readAll()));
}
