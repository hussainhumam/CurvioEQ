#include "sessionlistcontroller.h"

#include "audio/audiosessionenumerator.h"
#include "audio/audiosessionvolume.h"
#include "audio/audiopolicyrouter.h"
#include "ui/appconstants.h"
#include "ui/appiconprovider.h"
#include "ui/appsessiondelegate.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QHash>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QSize>
#include <QSlider>
#include <QStandardItem>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QToolButton>
#include <QWidget>
#include <QWidgetAction>

#include <algorithm>

namespace {

constexpr QColor kMuteMarkColor(220, 70, 70);

class SpeakerMuteButton : public QToolButton
{
public:
    explicit SpeakerMuteButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setAutoRaise(true);
        setFixedSize(16, 16);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setStyleSheet(QStringLiteral("QToolButton { padding: 0; margin: 0; border: none; }"));
    }

    void setPercent(int percent)
    {
        m_percent = std::clamp(percent, 0, 150);
        update();
        updateTip();
    }

    void setMuted(bool muted)
    {
        setChecked(muted);
        update();
        updateTip();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF bounds = QRectF(rect()).adjusted(1.0, 1.5, -1.0, -1.5);
        const QColor ink = palette().color(isEnabled() ? QPalette::Active : QPalette::Disabled,
                                          QPalette::ButtonText);

        QPainterPath speaker;
        const qreal left = bounds.left();
        const qreal midY = bounds.center().y();
        const qreal bodyRight = left + bounds.width() * 0.34;
        const qreal hornRight = left + bounds.width() * 0.48;
        speaker.moveTo(left, midY - bounds.height() * 0.14);
        speaker.lineTo(bodyRight, midY - bounds.height() * 0.14);
        speaker.lineTo(hornRight, bounds.top() + 0.4);
        speaker.lineTo(hornRight, bounds.bottom() - 0.4);
        speaker.lineTo(bodyRight, midY + bounds.height() * 0.14);
        speaker.lineTo(left, midY + bounds.height() * 0.14);
        speaker.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawPath(speaker);

        if (!isChecked()) {
            const int waves = m_percent <= 0 ? 0 : (m_percent < 40 ? 1 : (m_percent < 80 ? 2 : 3));
            QPen wavePen(ink, 1.1, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(wavePen);
            painter.setBrush(Qt::NoBrush);
            const QPointF origin(hornRight + 0.5, midY);
            for (int i = 0; i < waves; ++i) {
                const qreal radius = 1.9 + 1.8 * static_cast<qreal>(i);
                QRectF arc(origin.x() - radius, origin.y() - radius, radius * 2.0, radius * 2.0);
                painter.drawArc(arc, -50 * 16, 100 * 16);
            }
        } else {
            QPen slash(kMuteMarkColor, 1.4, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(slash);
            painter.drawLine(bounds.topLeft() + QPointF(0.4, 0.4), bounds.bottomRight() - QPointF(0.4, 0.4));
        }
    }

private:
    void updateTip()
    {
        setToolTip(isChecked() ? QStringLiteral("Muted") : QStringLiteral("Volume %1%").arg(m_percent));
    }

    int m_percent = 100;
};

} // namespace

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

void SessionListController::setStartupPresetBoundQuery(std::function<bool(unsigned long)> query)
{
    m_startupPresetBoundQuery = std::move(query);
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

QVector<unsigned long> SessionListController::processIds() const
{
    QVector<unsigned long> ids;
    if (!m_model) {
        return ids;
    }
    ids.reserve(m_model->rowCount());
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QStandardItem *item = m_model->item(row);
        if (!item) {
            continue;
        }
        const unsigned long pid = static_cast<unsigned long>(item->data(RoleProcessId).toULongLong());
        if (pid != 0) {
            ids.append(pid);
        }
    }
    return ids;
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

void SessionListController::setClipRecording(unsigned long processId, const QString &displayName)
{
    m_clipRecordingPid = processId;
    m_clipRecordingName = displayName;
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
    menu.setToolTipsVisible(true);
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

    QAction *startupAction = menu.addAction(QStringLiteral("Start at app startup"));
    startupAction->setCheckable(true);
    const bool startupBound = m_startupPresetBoundQuery && m_startupPresetBoundQuery(processId);
    startupAction->setChecked(startupBound);
    startupAction->setToolTip(
        QStringLiteral("Apply this preset whenever this app starts (CurvioEQ must be running)"));
    connect(startupAction, &QAction::triggered, this, [this, processId](bool checked) {
        emit startupPresetToggled(processId, checked);
    });

    auto *volumeRow = new QWidget(&menu);
    volumeRow->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    volumeRow->setFixedHeight(18);
    auto *volumeLayout = new QHBoxLayout(volumeRow);
    volumeLayout->setContentsMargins(8, 0, 8, 0);
    volumeLayout->setSpacing(3);
    auto *speakerButton = new SpeakerMuteButton(volumeRow);
    auto *volumeSlider = new QSlider(Qt::Horizontal, volumeRow);
    volumeSlider->setRange(AudioSessionVolume::kMinPercent, AudioSessionVolume::kMaxPercent);
    volumeSlider->setFixedSize(108, 14);
    volumeSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    volumeSlider->setSingleStep(1);
    volumeSlider->setPageStep(5);
    volumeSlider->setStyleSheet(QStringLiteral(
        "QSlider { min-height: 14px; max-height: 14px; }"
        "QSlider::groove:horizontal {"
        "  height: 2px; border: none; border-radius: 1px; background: palette(mid);"
        "}"
        "QSlider::sub-page:horizontal {"
        "  height: 2px; border: none; border-radius: 1px; background: palette(highlight);"
        "}"
        "QSlider::add-page:horizontal {"
        "  height: 2px; border: none; border-radius: 1px; background: palette(midlight);"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 8px; height: 8px; margin: -3px 0; border: none; border-radius: 4px;"
        "  background: palette(button-text);"
        "}"
        "QSlider::handle:horizontal:hover { background: palette(highlight); }"));
    volumeSlider->setToolTip(
        QStringLiteral("Windows mixer volume (0–100%). 101–150% is up to ~50% louder on the EQ output."));
    auto *percentLabel = new QLabel(QStringLiteral("100%"), volumeRow);
    QFont percentFont = percentLabel->font();
    percentFont.setPointSizeF(qMax(8.0, percentFont.pointSizeF() - 1.0));
    percentLabel->setFont(percentFont);
    percentLabel->setMargin(0);
    percentLabel->setIndent(0);
    percentLabel->setFixedWidth(percentLabel->fontMetrics().horizontalAdvance(QStringLiteral("150%")));
    percentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    percentLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    volumeLayout->addWidget(speakerButton);
    volumeLayout->addWidget(volumeSlider, 1);
    volumeLayout->addWidget(percentLabel);

    int percent = AudioSessionVolume::kUnityPercent;
    QString volumeError;
    if (m_boostPercent.contains(processId)) {
        percent = std::clamp(m_boostPercent.value(processId),
                             AudioSessionVolume::kMinPercent,
                             AudioSessionVolume::kMaxPercent);
        volumeSlider->setValue(percent);
        percentLabel->setText(QStringLiteral("%1%").arg(percent));
    } else {
        float currentLevel = 1.f;
        if (AudioSessionVolume::getMasterVolume(processId, &currentLevel, &volumeError)) {
            percent = std::clamp(static_cast<int>(currentLevel * 100.f + 0.5f),
                                 AudioSessionVolume::kMinPercent,
                                 AudioSessionVolume::kUnityPercent);
            volumeSlider->setValue(percent);
            percentLabel->setText(QStringLiteral("%1%").arg(percent));
        } else {
            speakerButton->setEnabled(false);
            volumeSlider->setEnabled(false);
            percentLabel->setText(QStringLiteral("\u2014"));
            volumeRow->setToolTip(volumeError);
        }
    }
    speakerButton->setPercent(percent);

    bool muted = false;
    if (AudioSessionVolume::getMute(processId, &muted)) {
        speakerButton->setMuted(muted);
    }

    connect(speakerButton, &QToolButton::clicked, this,
            [this, processId, speakerButton](bool checked) {
                if (!AudioSessionVolume::setMute(processId, checked)) {
                    speakerButton->setEnabled(false);
                    return;
                }
                speakerButton->setMuted(checked);
                refresh();
            });

    connect(volumeSlider, &QSlider::valueChanged, this,
            [this, processId, percentLabel, volumeSlider, speakerButton](int value) {
                constexpr int kSnapLow = AudioSessionVolume::kUnityPercent - 1;
                constexpr int kSnapHigh = AudioSessionVolume::kUnityPercent + 1;
                if (value >= kSnapLow && value <= kSnapHigh
                    && value != AudioSessionVolume::kUnityPercent) {
                    volumeSlider->blockSignals(true);
                    volumeSlider->setValue(AudioSessionVolume::kUnityPercent);
                    volumeSlider->blockSignals(false);
                    value = AudioSessionVolume::kUnityPercent;
                }
                percentLabel->setText(QStringLiteral("%1%").arg(value));
                speakerButton->setPercent(value);
                if (speakerButton->isChecked()) {
                    if (!AudioSessionVolume::setMute(processId, false)) {
                        volumeSlider->setEnabled(false);
                        return;
                    }
                    speakerButton->setMuted(false);
                    refresh();
                }
                const float windowsLevel =
                    static_cast<float>(std::min(value, AudioSessionVolume::kUnityPercent)) / 100.f;
                if (!AudioSessionVolume::setMasterVolume(processId, windowsLevel)) {
                    volumeSlider->setEnabled(false);
                    return;
                }
                if (value > AudioSessionVolume::kUnityPercent) {
                    m_boostPercent.insert(processId, value);
                } else {
                    m_boostPercent.remove(processId);
                }
                emit appVolumeChanged(processId, value);
            });

    auto *volumeAction = new QWidgetAction(&menu);
    volumeAction->setDefaultWidget(volumeRow);
    menu.addAction(volumeAction);

    QAction *soundModsAction = menu.addAction(QStringLiteral("Manage sound files…"));
    connect(soundModsAction, &QAction::triggered, this, [this, processId]() {
        emit soundModsRequested(processId);
    });

    if (m_clipRecordingPid == processId) {
        QAction *stopClipAction = menu.addAction(QStringLiteral("Stop and analyze"));
        connect(stopClipAction, &QAction::triggered, this, [this]() {
            emit stopClipAnalyzeRequested();
        });
    } else {
        QAction *recordClipAction = menu.addAction(QStringLiteral("Record clip"));
        if (m_clipRecordingPid != 0) {
            recordClipAction->setEnabled(false);
            recordClipAction->setToolTip(
                QStringLiteral("Already recording %1").arg(m_clipRecordingName));
        }
        connect(recordClipAction, &QAction::triggered, this, [this, processId]() {
            emit recordClipRequested(processId);
        });
    }

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
