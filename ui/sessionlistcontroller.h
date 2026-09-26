#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QModelIndex>
#include <QObject>
#include <QVector>

#include <functional>

class AppSessionDelegate;
class QEvent;
class QLabel;
class QListView;
class QStandardItem;
class QStandardItemModel;
class QTimer;

struct AudioSessionInfo;

class SessionListController : public QObject
{
    Q_OBJECT

public:
    enum ItemRole {
        RoleProcessId = Qt::UserRole,
        RoleOutputDeviceId = Qt::UserRole + 1,
        RoleOutputDeviceName = Qt::UserRole + 2,
        RoleDisplayName = Qt::UserRole + 3,
        RoleEqActive = Qt::UserRole + 4,
        RoleEqColor = Qt::UserRole + 5,
        RoleMuted = Qt::UserRole + 6,
    };

    SessionListController(QListView *listView, QLabel *countLabel, QObject *parent = nullptr);

    void setEqSessions(const QHash<unsigned long, QColor> &activeSessions);
    void setAutoRefreshEnabled(bool enabled);
    void refresh();

    unsigned long selectedProcessId() const;
    QVector<unsigned long> processIds() const;
    QString displayNameForPid(unsigned long pid) const;
    int appCount() const;
    void setClipRecording(unsigned long processId, const QString &displayName);
    void setStartupPresetBoundQuery(std::function<bool(unsigned long)> query);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void selectionChanged();
    void refreshRequested();
    void logMessage(const QString &level, const QString &message);
    void errorOccurred(const QString &title, const QString &message);
    void enableEqRequested(unsigned long processId);
    void disableEqRequested(unsigned long processId);
    void soundModsRequested(unsigned long processId);
    void recordClipRequested(unsigned long processId);
    void stopClipAnalyzeRequested();
    void appVolumeChanged(unsigned long processId, int percent);
    void startupPresetToggled(unsigned long processId, bool enable);

private slots:
    void onTimer();
    void showContextMenu(const QPoint &position);

private:
    unsigned long processIdAt(const QModelIndex &index) const;
    QString currentOutputDeviceIdAt(const QModelIndex &index) const;
    void updateCountLabel();
    void applySessionToItem(QStandardItem *item, const AudioSessionInfo &session);
    void toggleEqAt(const QModelIndex &index);
    int rowForProcessId(unsigned long processId) const;

    QListView *m_listView = nullptr;
    QLabel *m_countLabel = nullptr;
    QStandardItemModel *m_model = nullptr;
    AppSessionDelegate *m_delegate = nullptr;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_lastPress;
    QHash<unsigned long, QColor> m_eqSessions;
    QHash<unsigned long, int> m_boostPercent;
    unsigned long m_clipRecordingPid = 0;
    QString m_clipRecordingName;
    std::function<bool(unsigned long)> m_startupPresetBoundQuery;
};
