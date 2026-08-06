#include "appsessiondelegate.h"

#include <QApplication>
#include <QPainter>
#include <QFontMetrics>

namespace {
constexpr int kRowHeight = 52;
constexpr int kIconSize = 32;
constexpr int kPadding = 8;
constexpr int kBadgeDotRadius = 4;
constexpr QColor kSelectionOverlay(128, 128, 128, 24);
constexpr QColor kSelectionBorder(100, 100, 100, 60);
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

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.showDecorationSelected = false;
    opt.backgroundBrush = Qt::NoBrush;
    opt.state &= ~QStyle::State_MouseOver;
    opt.state &= ~QStyle::State_HasFocus;

    const QRect rowRect = opt.rect;
    const bool selected = opt.state.testFlag(QStyle::State_Selected);
    const bool eqActive = index.data(RoleEqActive).toBool();

    painter->fillRect(rowRect, opt.palette.window().color());

    if (selected && !eqActive) {
        painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter->fillRect(rowRect, kSelectionOverlay);
    }

    const QIcon icon = index.data(RoleIcon).value<QIcon>();
    const QString name = index.data(Qt::DisplayRole).toString();
    const QString deviceName = index.data(RoleOutputDeviceName).toString();
    const qulonglong pid = index.data(RoleProcessId).toULongLong();

    const int iconX = rowRect.left() + kPadding;
    const int iconY = rowRect.top() + (rowRect.height() - kIconSize) / 2;
    if (!icon.isNull()) {
        icon.paint(painter, QRect(iconX, iconY, kIconSize, kIconSize));
    } else {
        painter->setPen(Qt::gray);
        painter->drawRect(QRect(iconX, iconY, kIconSize, kIconSize));
        painter->drawText(QRect(iconX, iconY, kIconSize, kIconSize),
                          Qt::AlignCenter, QStringLiteral("?"));
    }

    const int textLeft = iconX + kIconSize + kPadding;
    const int textWidth = rowRect.right() - textLeft - kPadding;

    QFont nameFont = opt.font;
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(opt.palette.text().color());

    const QRect nameRect(textLeft, rowRect.top() + 6, textWidth, 20);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(name, Qt::ElideRight, textWidth));

    QFont pidFont = opt.font;
    pidFont.setPointSize(opt.font.pointSize() - 1);
    painter->setFont(pidFont);
    painter->setPen(opt.palette.color(QPalette::Disabled, QPalette::Text));

    const QString sublineText = deviceName.isEmpty()
        ? QStringLiteral("PID %1").arg(pid)
        : deviceName;
    const QRect pidRect(textLeft, rowRect.top() + 26, textWidth, 18);
    painter->drawText(pidRect, Qt::AlignLeft | Qt::AlignVCenter,
                      painter->fontMetrics().elidedText(sublineText, Qt::ElideRight, textWidth));

    if (eqActive) {
        const QString badgeText = QStringLiteral("EQ ON");
        QFont badgeFont = opt.font;
        badgeFont.setBold(true);
        badgeFont.setPointSize(opt.font.pointSize() - 1);
        painter->setFont(badgeFont);

        const QFontMetrics badgeMetrics(badgeFont);
        const int badgeWidth = badgeMetrics.horizontalAdvance(badgeText) + kBadgeDotRadius * 2 + 6;
        const int badgeHeight = badgeMetrics.height() + 4;
        const int badgeX = rowRect.right() - kPadding - badgeWidth;
        const int badgeY = rowRect.top() + (rowRect.height() - badgeHeight) / 2;

        const int dotX = badgeX + kBadgeDotRadius + 2;
        const int dotY = badgeY + badgeHeight / 2;
        const QColor badgeColor(70, 130, 220);
        painter->setBrush(badgeColor);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(QPoint(dotX, dotY), kBadgeDotRadius, kBadgeDotRadius);

        painter->setPen(badgeColor);
        painter->drawText(QRect(badgeX + kBadgeDotRadius * 2 + 4, badgeY, badgeWidth, badgeHeight),
                          Qt::AlignLeft | Qt::AlignVCenter, badgeText);
    }

    if (selected) {
        painter->setPen(kSelectionBorder);
        painter->drawRect(rowRect.adjusted(0, 0, -1, -1));
    }

    painter->restore();
}

QSize AppSessionDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    Q_UNUSED(option)
    Q_UNUSED(index)
    return QSize(0, kRowHeight);
}
