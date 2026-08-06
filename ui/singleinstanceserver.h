#pragma once

#include <QObject>

class QLocalServer;

class SingleInstanceServer : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstanceServer(QObject *parent = nullptr);

    static bool notifyExistingInstance();
    void listen();

signals:
    void showRequested();
    void listenFailed(const QString &errorMessage);

private:
    QLocalServer *m_server = nullptr;
};
