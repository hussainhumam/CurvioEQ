#include "traycontroller.h"

#include "ui/appconstants.h"
#include "ui/appiconprovider.h"

#include <QAction>
#include <QMainWindow>
#include <QMenu>
#include <QStyle>
#include <QSystemTrayIcon>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

TrayController::TrayController(QMainWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

void TrayController::setup()
{
    m_available = QSystemTrayIcon::isSystemTrayAvailable();
    if (!m_available) {
        emit logMessage(QStringLiteral("WARN"), QStringLiteral("System tray is not available on this system"));
        return;
    }

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayMenu = new QMenu(m_window);

    QIcon trayIcon = AppIconProvider::appIcon();
    if (trayIcon.isNull()) {
        trayIcon = AppIconProvider::iconForProcess(static_cast<unsigned long>(GetCurrentProcessId()));
    }
    if (trayIcon.isNull()) {
        trayIcon = m_window->style()->standardIcon(QStyle::SP_MediaVolume);
    }
    m_trayIcon->setIcon(trayIcon);
    m_window->setWindowIcon(trayIcon);
    m_trayIcon->setToolTip(QString::fromLatin1(AppConstants::kAppDisplayName));

    m_trayMenu->addAction(QStringLiteral("Show Window"), this, [this]() { emit showWindowRequested(); });
    m_enableAction = m_trayMenu->addAction(QStringLiteral("Enable EQ"), this, [this]() { emit enableEqRequested(); });
    m_disableAction = m_trayMenu->addAction(QStringLiteral("Disable EQ"), this, [this]() { emit disableEqRequested(); });
    m_trayMenu->addSeparator();
    m_trayMenu->addAction(QStringLiteral("Quit"), this, [this]() { emit quitRequested(); });

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &TrayController::onTrayActivated);
    m_trayIcon->show();
}

void TrayController::updateEqControls(bool canEnable, bool canDisable)
{
    if (m_enableAction) {
        m_enableAction->setEnabled(canEnable);
    }
    if (m_disableAction) {
        m_disableAction->setEnabled(canDisable);
    }
}

bool TrayController::isAvailable() const
{
    return m_available;
}

void TrayController::showCriticalMessage(const QString &title, const QString &message)
{
    if (m_trayIcon) {
        m_trayIcon->showMessage(title,
                                message,
                                QSystemTrayIcon::Critical,
                                AppConstants::kTrayMessageDurationMs);
    }
}

void TrayController::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger) {
        emit showWindowRequested();
    }
}
