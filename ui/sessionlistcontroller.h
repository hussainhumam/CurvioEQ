#pragma once



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



    void setEqActive(unsigned long activePid, bool engineRunning);

    void refresh();



    unsigned long selectedProcessId() const;

    QString displayNameForPid(unsigned long pid) const;

    int appCount() const;



signals:

    void selectionChanged();

    void refreshRequested();

    void logMessage(const QString &level, const QString &message);

    void errorOccurred(const QString &title, const QString &message);



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

    unsigned long m_eqActivePid = 0;

    bool m_engineRunning = false;

};

