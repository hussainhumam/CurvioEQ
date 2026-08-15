#pragma once

#include <QStyledItemDelegate>

class AppSessionDelegate : public QStyledItemDelegate
{
public:
    explicit AppSessionDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    static constexpr int RoleProcessId = Qt::UserRole;
    static constexpr int RoleIcon = Qt::UserRole + 1;
    static constexpr int RoleEqActive = Qt::UserRole + 2;
    static constexpr int RoleEqColor = Qt::UserRole + 5;
    static constexpr int RoleOutputDeviceId = Qt::UserRole + 3;
    static constexpr int RoleOutputDeviceName = Qt::UserRole + 4;
};
