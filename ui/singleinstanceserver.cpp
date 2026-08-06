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

bool SingleInstanceServer::notifyExistingInstance()
{
    QLocalSocket socket;
    socket.connectToServer(QString::fromLatin1(kSingleInstanceServerName));
    if (!socket.waitForConnected(500)) {
        return false;
    }

    socket.write("show");
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

        if (socket->waitForReadyRead(500) && socket->readAll().trimmed() == QByteArray("show")) {
            emit showRequested();
        }
        socket->deleteLater();
    });
}
