#include "sessionlistcontroller.h"

#include "audio/audiosessionenumerator.h"
#include "audio/audiopolicyrouter.h"
#include "ui/appconstants.h"
#include "ui/appiconprovider.h"
#include "ui/appsessiondelegate.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QSize>
#include <QStandardItem>
#include <QStringList>
#include <QTimer>
#include <QVector>

#include <algorithm>

SessionListController::SessionListController(QListView *listView, QLabel *countLabel, QObject *parent)
    : QObject(parent)
    , m_listView(listView)
    , m_countLabel(countLabel)
    , m_model(new QStandardItemModel(this))
    , m_delegate(new AppSessionDelegate(this))
    , m_timer(new QTimer(this))
{
    m_listView->setModel(m_model);
    m_listView->setItemDelegate(m_delegate);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setAlternatingRowColors(false);
    m_listView->setIconSize(QSize(32, 32));

    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &, const QModelIndex &) {
                emit selectionChanged();
            });

    connect(m_timer, &QTimer::timeout, this, &SessionListController::onTimer);
    m_timer->setInterval(AppConstants::kSessionRefreshIntervalActiveMs);

    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_listView, &QWidget::customContextMenuRequested, this, &SessionListController::showContextMenu);
    m_listView->viewport()->installEventFilter(this);
}

void SessionListController::setEqSessions(const QHash<unsigned long, QColor> &activeSessions)
{
    m_eqSessions = activeSessions;
}

void SessionListController::setAutoRefreshEnabled(bool enabled)
{
    if (enabled) {
        if (!m_timer->isActive()) {
            m_timer->start();
        }
    } else {
        m_timer->stop();
    }
}

void SessionListController::refresh()
{
    const unsigned long selectedPid = selectedProcessId();
    const QVector<AudioSessionInfo> sessions = AudioSessionEnumerator::listActiveSessions();

    QHash<unsigned long, AudioSessionInfo> uniqueApps;
    for (const AudioSessionInfo &session : sessions) {
        if (!uniqueApps.contains(session.processId)) {
            uniqueApps.insert(session.processId, session);
        }
    }

    QVector<AudioSessionInfo> apps;
    apps.reserve(uniqueApps.size());
    for (const AudioSessionInfo &session : uniqueApps) {
        apps.append(session);
    }
    std::sort(apps.begin(), apps.end(), [](const AudioSessionInfo &left, const AudioSessionInfo &right) {
        const int nameCmp = QString::localeAwareCompare(left.displayName, right.displayName);
        if (nameCmp != 0) {
            return nameCmp < 0;
        }
        return left.processId < right.processId;
    });

    QSet<unsigned long> wantedPids;
    wantedPids.reserve(apps.size());
    for (const AudioSessionInfo &session : apps) {
        wantedPids.insert(session.processId);
    }

    for (int row = m_model->rowCount() - 1; row >= 0; --row) {
        const unsigned long pid =
            static_cast<unsigned long>(m_model->item(row)->data(RoleProcessId).toULongLong());
        if (!wantedPids.contains(pid)) {
            m_model->removeRow(row);
        }
    }

    for (int target = 0; target < apps.size(); ++target) {
        const AudioSessionInfo &session = apps[target];
        const int found = rowForProcessId(session.processId);
        if (found < 0) {
            auto *item = new QStandardItem();
            applySessionToItem(item, session);
            m_model->insertRow(target, item);
            continue;
        }
        if (found != target) {
            m_model->insertRow(target, m_model->takeRow(found));
        }
        applySessionToItem(m_model->item(target), session);
    }

    updateCountLabel();

    if (selectedPid != 0) {
        const int row = rowForProcessId(selectedPid);
        if (row >= 0) {
            m_listView->setCurrentIndex(m_model->index(row, 0));
        }
    }
}

unsigned long SessionListController::selectedProcessId() const
{
    return processIdAt(m_listView->currentIndex());
}

unsigned long SessionListController::processIdAt(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return 0;
    }

    const QStandardItem *item = m_model->itemFromIndex(index);
    if (!item) {
        return 0;
    }

    return static_cast<unsigned long>(item->data(RoleProcessId).toULongLong());
}

QString SessionListController::currentOutputDeviceIdAt(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return {};
    }

    const QStandardItem *item = m_model->itemFromIndex(index);
    if (!item) {
        return {};
    }

    QString deviceId = item->data(RoleOutputDeviceId).toString();
    if (deviceId.isEmpty()) {
        const unsigned long processId = processIdAt(index);
        if (processId != 0) {
            deviceId = AudioPolicyRouter::persistedRenderDeviceId(processId);
        }
    }
    return deviceId;
}

QString SessionListController::displayNameForPid(unsigned long pid) const
{
    if (pid == 0) {
        return {};
    }

    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QStandardItem *item = m_model->item(row);
        if (!item) {
            continue;
        }
        if (item->data(RoleProcessId).toULongLong() == pid) {
            const QString name = item->data(RoleDisplayName).toString();
            return name.isEmpty() ? item->text() : name;
        }
    }
    return QStringLiteral("PID %1").arg(pid);
}

int SessionListController::appCount() const
{
    return m_model ? m_model->rowCount() : 0;
}

void SessionListController::updateCountLabel()
{
    if (!m_countLabel) {
        return;
    }
    const int count = appCount();
    m_countLabel->setText(count > 0
        ? QStringLiteral("%1 app(s) playing audio").arg(count)
        : QStringLiteral("Active audio sessions"));
}

void SessionListController::applySessionToItem(QStandardItem *item, const AudioSessionInfo &session)
{
    if (!item) {
        return;
    }

    const bool eqActive = m_eqSessions.contains(session.processId);
    const QString displayName = session.displayName;

    item->setEditable(false);
    item->setText(displayName);
    item->setIcon(AppIconProvider::iconForProcess(session.processId));
    item->setData(static_cast<qulonglong>(session.processId), RoleProcessId);
    item->setData(session.deviceId, RoleOutputDeviceId);
    item->setData(session.deviceName, RoleOutputDeviceName);
    item->setData(displayName, RoleDisplayName);
    item->setData(eqActive, RoleEqActive);
    item->setData(m_eqSessions.value(session.processId), RoleEqColor);
    item->setData(session.muted, RoleMuted);

    QStringList tooltipParts;
    if (eqActive) {
        tooltipParts.append(QStringLiteral("EQ enabled"));
    }
    if (session.muted) {
        tooltipParts.append(QStringLiteral("muted"));
    }
    tooltipParts.append(session.deviceName.isEmpty()
        ? QStringLiteral("PID %1").arg(session.processId)
        : session.deviceName);
    item->setToolTip(tooltipParts.join(QStringLiteral(" - ")));
}

void SessionListController::toggleEqAt(const QModelIndex &index)
{
    const unsigned long processId = processIdAt(index);
    if (processId == 0) {
        return;
    }
    if (m_eqSessions.contains(processId)) {
        emit disableEqRequested(processId);
    } else {
        emit enableEqRequested(processId);
    }
}

int SessionListController::rowForProcessId(unsigned long processId) const
{
    if (processId == 0) {
        return -1;
    }
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QStandardItem *item = m_model->item(row);
        if (item && item->data(RoleProcessId).toULongLong() == processId) {
            return row;
        }
    }
    return -1;
}

bool SessionListController::eventFilter(QObject *watched, QEvent *event)
{
    if (m_listView && watched == m_listView->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_lastPress.start();
            }
            break;
        }
        case QEvent::MouseButtonDblClick: {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                toggleEqAt(m_listView->indexAt(mouse->pos()));
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void SessionListController::onTimer()
{
    if (m_lastPress.isValid() && m_lastPress.elapsed() < QApplication::doubleClickInterval()) {
        return;
    }
    refresh();
    emit refreshRequested();
}

void SessionListController::showContextMenu(const QPoint &position)
{
    const QModelIndex index = m_listView->indexAt(position);
    if (!index.isValid()) {
        return;
    }

    m_listView->setCurrentIndex(index);

    const unsigned long processId = processIdAt(index);
    if (processId == 0) {
        return;
    }

    QMenu menu(m_listView);
    const bool eqActive = m_eqSessions.contains(processId);
    if (eqActive) {
        QAction *disableEqAction = menu.addAction(QStringLiteral("Disable EQ"));
        connect(disableEqAction, &QAction::triggered, this, [this, processId]() {
            emit disableEqRequested(processId);
        });
    } else {
        QAction *enableEqAction = menu.addAction(QStringLiteral("Enable EQ"));
        connect(enableEqAction, &QAction::triggered, this, [this, processId]() {
            emit enableEqRequested(processId);
        });
    }

    menu.addSeparator();

    QAction *soundModsAction = menu.addAction(QStringLiteral("Manage sound files…"));
    connect(soundModsAction, &QAction::triggered, this, [this, processId]() {
        emit soundModsRequested(processId);
    });

    menu.addSeparator();

    if (!AudioPolicyRouter::isRoutingSupported()) {
        QAction *unsupportedAction = menu.addAction(QStringLiteral("Output device routing unavailable"));
        unsupportedAction->setEnabled(false);
        menu.exec(m_listView->viewport()->mapToGlobal(position));
        return;
    }

    const QString currentDeviceId = currentOutputDeviceIdAt(index);
    const QString appName = index.data(RoleDisplayName).toString();

    QMenu *outputMenu = menu.addMenu(QStringLiteral("Output device"));
    if (eqActive) {
        outputMenu->setToolTipsVisible(true);
        outputMenu->setToolTip(
            QStringLiteral("Output is managed by EQ routing while EQ is active. Disable EQ to change it."));
        outputMenu->setEnabled(false);
    }

    const QVector<AudioRenderDeviceInfo> devices = AudioPolicyRouter::listRenderDevices();

    auto *actionGroup = new QActionGroup(&menu);
    actionGroup->setExclusive(true);

    for (const AudioRenderDeviceInfo &device : devices) {
        QString label = device.friendlyName;
        if (device.isDefault) {
            label += QStringLiteral(" (Windows default)");
        }

        auto *action = outputMenu->addAction(label);
        action->setCheckable(true);
        action->setActionGroup(actionGroup);
        action->setData(device.id);
        action->setEnabled(!eqActive);

        if (!currentDeviceId.isEmpty() && device.id == currentDeviceId) {
            action->setChecked(true);
        }

        connect(action, &QAction::triggered, this, [this, processId, appName, device]() {
            QString errorMessage;
            if (!AudioPolicyRouter::routeProcessToDevice(processId, device.id, &errorMessage)) {
                emit errorOccurred(QStringLiteral("Output device"), errorMessage);
                return;
            }

            emit logMessage(QStringLiteral("INFO"),
                            QStringLiteral("Routed %1 to %2").arg(appName, device.friendlyName));
            refresh();
        });
    }

    menu.addSeparator();

    QAction *resetAction = menu.addAction(QStringLiteral("Use Windows default"));
    resetAction->setEnabled(!eqActive);
    if (eqActive) {
        resetAction->setToolTip(
            QStringLiteral("Output is managed by EQ routing while EQ is active. Disable EQ to restore default."));
    }

    connect(resetAction, &QAction::triggered, this, [this, processId, appName]() {
        QString errorMessage;
        if (!AudioPolicyRouter::clearProcessRouting(processId, &errorMessage)) {
            emit errorOccurred(QStringLiteral("Output device"), errorMessage);
            return;
        }

        emit logMessage(QStringLiteral("INFO"),
                        QStringLiteral("Reset output device for %1 to Windows default").arg(appName));
        refresh();
    });

    menu.exec(m_listView->viewport()->mapToGlobal(position));
}
