#pragma once

#include <QColor>
#include <QHash>
#include <QObject>

class AppSessionDelegate;
class QLabel;
class QListView;
class QStandardItemModel;
class QTimer;

class SessionListController : public QObject
{
    Q_OBJECT

public:
    SessionListController(QListView *listView,
                          QLabel *countLabel,
                          QLabel *emptyLabel,
                          QObject *parent = nullptr);

    void setEqSessions(const QHash<unsigned long, QColor> &activeSessions);
    void setAutoRefreshEnabled(bool enabled);
    void refresh();

    unsigned long selectedProcessId() const;
    QString displayNameForPid(unsigned long pid) const;
    int appCount() const;

signals:
    void selectionChanged();
    void refreshRequested();
    void logMessage(const QString &level, const QString &message);
    void errorOccurred(const QString &title, const QString &message);
    void enableEqRequested(unsigned long processId);
    void disableEqRequested(unsigned long processId);
    void soundModsRequested(unsigned long processId);

private slots:
    void onTimer();
    void showContextMenu(const QPoint &position);

private:
    void updateEmptyState();
    unsigned long processIdAt(const QModelIndex &index) const;
    QString currentOutputDeviceIdAt(const QModelIndex &index) const;

    QListView *m_listView = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_emptyLabel = nullptr;
    QStandardItemModel *m_model = nullptr;
    AppSessionDelegate *m_delegate = nullptr;
    QTimer *m_timer = nullptr;
    QHash<unsigned long, QColor> m_eqSessions;
};
