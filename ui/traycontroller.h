#pragma once

#include <QObject>
#include <QSystemTrayIcon>

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
    void updateEqControls(bool canEnable, bool canDisable);
    bool isAvailable() const;

    void showCriticalMessage(const QString &title, const QString &message);

signals:
    void showWindowRequested();
    void enableEqRequested();
    void disableEqRequested();
    void quitRequested();
    void logMessage(const QString &level, const QString &message);

private:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

    QMainWindow *m_window = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QAction *m_enableAction = nullptr;
    QAction *m_disableAction = nullptr;
    bool m_available = false;
};
