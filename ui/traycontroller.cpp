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

    m_showWindowAction = m_trayMenu->addAction(QStringLiteral("Show Window"), this, [this]() {
        emit showWindowRequested();
    });
    m_eqSeparatorAction = m_trayMenu->addSeparator();
    m_eqSeparatorAction->setVisible(false);
    m_quitSeparatorAction = m_trayMenu->addSeparator();
    m_quitAction = m_trayMenu->addAction(QStringLiteral("Quit"), this, [this]() { emit quitRequested(); });

    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &TrayController::onTrayActivated);
    m_trayIcon->show();
}

void TrayController::clearEqActions()
{
    for (QAction *action : m_eqActions) {
        m_trayMenu->removeAction(action);
        delete action;
    }
    m_eqActions.clear();
}

void TrayController::updateEqSessions(const QVector<ConfiguredEqSession> &sessions)
{
    if (!m_trayMenu) {
        return;
    }

    clearEqActions();

    const bool hasEqRows = !sessions.isEmpty();
    if (m_eqSeparatorAction) {
        m_eqSeparatorAction->setVisible(hasEqRows);
    }

    for (const ConfiguredEqSession &session : sessions) {
        const QString label = session.active
                                  ? QStringLiteral("Disable %1").arg(session.displayName)
                                  : QStringLiteral("Enable %1").arg(session.displayName);

        const unsigned long processId = session.processId;
        auto *action = new QAction(label, m_trayMenu);
        action->setEnabled(true);
        connect(action, &QAction::triggered, this, [this, processId]() {
            emit toggleEqForProcessRequested(processId);
        });

        m_trayMenu->insertAction(m_quitSeparatorAction, action);
        m_eqActions.push_back(action);
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
