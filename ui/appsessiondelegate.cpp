#include "appsessiondelegate.h"

#include "sessionlistcontroller.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStringList>

namespace {
constexpr int kRowHeight = 52;
constexpr int kIconSize = 32;
constexpr int kPadding = 8;
constexpr int kDotRadius = 8;
constexpr int kDotMargin = 10;
constexpr int kRightGutter = kDotMargin + kDotRadius * 2 + 6;
}

AppSessionDelegate::AppSessionDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void AppSessionDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    if (!index.isValid()) {
        return;
    }

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.icon = QIcon();
    opt.features &= ~QStyleOptionViewItem::HasDisplay;
    opt.features &= ~QStyleOptionViewItem::HasDecoration;

    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

    const bool selected = opt.state.testFlag(QStyle::State_Selected);
    const bool eqActive = index.data(SessionListController::RoleEqActive).toBool();
    const bool muted = index.data(SessionListController::RoleMuted).toBool();
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const QString name = index.data(SessionListController::RoleDisplayName).toString();
    const QString deviceName = index.data(SessionListController::RoleOutputDeviceName).toString();
    const qulonglong pid = index.data(SessionListController::RoleProcessId).toULongLong();

    QStringList statusParts;
    statusParts.append(deviceName.isEmpty() ? QStringLiteral("PID %1").arg(pid) : deviceName);
    if (eqActive) {
        statusParts.append(QStringLiteral("EQ enabled"));
    }
    if (muted) {
        statusParts.append(QStringLiteral("muted"));
    }
    const QString status = statusParts.join(QStringLiteral(" - "));

    const QRect rowRect = option.rect;
    const int contentRight = eqActive ? rowRect.right() - kRightGutter : rowRect.right() - kPadding;

    const int iconX = rowRect.left() + kPadding;
    const int iconY = rowRect.top() + (rowRect.height() - kIconSize) / 2;
    const QRect iconRect(iconX, iconY, kIconSize, kIconSize);
    if (!icon.isNull()) {
        icon.paint(painter, iconRect);
    }

    const int textLeft = iconX + kIconSize + kPadding;
    const int textWidth = qMax(0, contentRight - textLeft);

    const QPalette::ColorRole textRole = selected ? QPalette::HighlightedText : QPalette::Text;
    const QPalette::ColorRole statusRole = selected ? QPalette::HighlightedText : QPalette::PlaceholderText;

    QFont nameFont = opt.font;
    nameFont.setBold(true);
    nameFont.setPointSize(opt.font.pointSize() + 2);
    painter->setFont(nameFont);
    painter->setPen(opt.palette.color(textRole));

    const QRect nameRect(textLeft, rowRect.top() + 6, textWidth, 22);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(name, Qt::ElideRight, textWidth));

    QFont statusFont = opt.font;
    painter->setFont(statusFont);
    painter->setPen(opt.palette.color(statusRole));
    const QRect statusRect(textLeft, rowRect.top() + 28, textWidth, 18);
    painter->drawText(statusRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(status, Qt::ElideRight, textWidth));

    if (!eqActive) {
        return;
    }

    QColor color = index.data(SessionListController::RoleEqColor).value<QColor>();
    if (!color.isValid()) {
        color = QColor(70, 130, 220);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    const QPoint center(rowRect.right() - kDotMargin - kDotRadius, rowRect.center().y());
    painter->drawEllipse(center, kDotRadius, kDotRadius);
    painter->restore();
}

QSize AppSessionDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(0, kRowHeight);
}
