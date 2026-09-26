#include "singleinstanceserver.h"

#include "ui/appconstants.h"

#include <QLocalServer>
#include <QLocalSocket>

namespace {
constexpr auto kSingleInstanceServerName = AppConstants::kAppId;
}

SingleInstanceServer::SingleInstanceServer(QObject *parent)
    : QObject(parent)
{
}

bool SingleInstanceServer::notifyExistingInstance(const QByteArray &payload)
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kSingleInstanceServerName));
    if (!socket.waitForConnected(500)) {
        return false;
    }

    socket.write(payload);
    socket.flush();
    socket.waitForBytesWritten(500);
    return true;
}

void SingleInstanceServer::listen()
{
    QLocalServer::removeServer(QString::fromLatin1(kSingleInstanceServerName));

    m_server = new QLocalServer(this);
    static bool listenFailedLogged = false;
    if (!m_server->listen(QString::fromLatin1(kSingleInstanceServerName))) {
        if (!listenFailedLogged) {
            listenFailedLogged = true;
            emit listenFailed(m_server->errorString());
        }
        return;
    }

    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            return;
        }

        if (socket->waitForReadyRead(500)) {
            const QByteArray data = socket->readAll().trimmed();
            if (data == QByteArrayLiteral("show")) {
                emit showRequested();
            } else if (data.startsWith(QByteArrayLiteral("SAS|"))) {
                emit startAtAppStartupRequested(QString::fromUtf8(data.mid(4)));
            }
        }
        socket->deleteLater();
    });
}
