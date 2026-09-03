#pragma once

#include <QStyledItemDelegate>

class AppSessionDelegate : public QStyledItemDelegate
{
public:
    explicit AppSessionDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
