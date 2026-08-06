#include "sessionlistcontroller.h"



#include "audio/audiosessionenumerator.h"

#include "audio/audiopolicyrouter.h"

#include "ui/appconstants.h"

#include "ui/appiconprovider.h"

#include "ui/appsessiondelegate.h"



#include <QAction>

#include <QActionGroup>

#include <QHash>

#include <QItemSelectionModel>

#include <QLabel>

#include <QListView>

#include <QMenu>

#include <QStandardItem>

#include <QTimer>



SessionListController::SessionListController(QListView *listView,

                                             QLabel *countLabel,

                                             QLabel *emptyLabel,

                                             QObject *parent)

    : QObject(parent)

    , m_listView(listView)

    , m_countLabel(countLabel)

    , m_emptyLabel(emptyLabel)

    , m_model(new QStandardItemModel(this))

    , m_delegate(new AppSessionDelegate(this))

    , m_timer(new QTimer(this))

{

    m_listView->setModel(m_model);

    m_listView->setItemDelegate(m_delegate);

    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);

    m_listView->setAlternatingRowColors(false);

    m_listView->setSpacing(2);

    m_listView->setStyleSheet(QStringLiteral(

        "QListView::item:hover { background: transparent; }"

        "QListView::item:selected { background: transparent; }"

        "QListView::item:selected:hover { background: transparent; }"));



    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged, this,

            [this](const QModelIndex &current, const QModelIndex &) {

                if (current.isValid()) {

                    m_listView->viewport()->update(m_listView->visualRect(current));

                }

                emit selectionChanged();

            });



    connect(m_timer, &QTimer::timeout, this, &SessionListController::onTimer);

    m_timer->start(AppConstants::kSessionRefreshIntervalMs);



    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_listView, &QWidget::customContextMenuRequested, this, &SessionListController::showContextMenu);

}



void SessionListController::setEqActive(unsigned long activePid, bool engineRunning)

{

    m_eqActivePid = activePid;

    m_engineRunning = engineRunning;

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



    m_model->clear();

    for (const AudioSessionInfo &session : uniqueApps) {

        auto *item = new QStandardItem(session.displayName);

        item->setData(static_cast<qulonglong>(session.processId), AppSessionDelegate::RoleProcessId);

        item->setData(AppIconProvider::iconForProcess(session.processId), AppSessionDelegate::RoleIcon);



        const bool eqActive = m_engineRunning && session.processId == m_eqActivePid;

        item->setData(eqActive, AppSessionDelegate::RoleEqActive);

        item->setData(session.deviceId, AppSessionDelegate::RoleOutputDeviceId);

        item->setData(session.deviceName, AppSessionDelegate::RoleOutputDeviceName);



        m_model->appendRow(item);

    }



    updateEmptyState();



    if (selectedPid != 0) {

        for (int row = 0; row < m_model->rowCount(); ++row) {

            const qulonglong pid = m_model->item(row)->data(AppSessionDelegate::RoleProcessId).toULongLong();

            if (pid == selectedPid) {

                m_listView->setCurrentIndex(m_model->index(row, 0));

                break;

            }

        }

    }



    const QModelIndex current = m_listView->currentIndex();

    if (current.isValid()) {

        m_listView->viewport()->update(m_listView->visualRect(current));

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



    return static_cast<unsigned long>(item->data(AppSessionDelegate::RoleProcessId).toULongLong());

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



    QString deviceId = item->data(AppSessionDelegate::RoleOutputDeviceId).toString();

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

        if (item->data(AppSessionDelegate::RoleProcessId).toULongLong() == pid) {

            return item->text();

        }

    }

    return QStringLiteral("PID %1").arg(pid);

}



int SessionListController::appCount() const

{

    return m_model ? m_model->rowCount() : 0;

}



void SessionListController::onTimer()

{

    refresh();

    emit refreshRequested();

}



void SessionListController::updateEmptyState()

{

    const bool hasApps = m_model->rowCount() > 0;

    m_listView->setVisible(hasApps);

    m_emptyLabel->setVisible(!hasApps);

    m_countLabel->setText(

        hasApps ? QStringLiteral("%1 app(s) playing audio").arg(m_model->rowCount())

                : QStringLiteral("Active audio sessions"));

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



    if (!AudioPolicyRouter::isRoutingSupported()) {

        QAction *unsupportedAction = menu.addAction(QStringLiteral("Output device routing unavailable"));

        unsupportedAction->setEnabled(false);

        menu.exec(m_listView->viewport()->mapToGlobal(position));

        return;

    }



    const QString currentDeviceId = currentOutputDeviceIdAt(index);

    const QString appName = index.data(Qt::DisplayRole).toString();



    QMenu *outputMenu = menu.addMenu(QStringLiteral("Output device"));

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

