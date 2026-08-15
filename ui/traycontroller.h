#pragma once

#include "ui/eqsessionmanager.h"

#include <QList>
#include <QObject>
#include <QSystemTrayIcon>
#include <QVector>

class QAction;
class QMainWindow;
class QMenu;
class QString;

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(QMainWindow *window, QObject *parent = nullptr);

    void setup();
    void updateEqSessions(const QVector<ConfiguredEqSession> &sessions);
    bool isAvailable() const;

    void showCriticalMessage(const QString &title, const QString &message);

signals:
    void showWindowRequested();
    void toggleEqForProcessRequested(unsigned long processId);
    void quitRequested();
    void logMessage(const QString &level, const QString &message);

private:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void clearEqActions();

    QMainWindow *m_window = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_showWindowAction = nullptr;
    QAction *m_eqSeparatorAction = nullptr;
    QAction *m_quitSeparatorAction = nullptr;
    QAction *m_quitAction = nullptr;
    QList<QAction *> m_eqActions;
    bool m_available = false;
};
