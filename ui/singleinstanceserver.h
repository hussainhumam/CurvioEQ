#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QLocalServer;

class SingleInstanceServer : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceServer(QObject *parent = nullptr);

    static bool notifyExistingInstance(const QByteArray &payload = QByteArrayLiteral("show"));
    void listen();

signals:
    void showRequested();
    void startAtAppStartupRequested(const QString &exePath);
    void listenFailed(const QString &errorMessage);

private:
    QLocalServer *m_server = nullptr;
};
