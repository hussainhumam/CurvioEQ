#include "eqcolorpalette.h"

#include <QAction>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QPixmap>
#include <QToolButton>

namespace {

constexpr QColor kDefaultColor(70, 130, 220);

} // namespace

QColor EqColorPalette::presetColorAt(int index)
{
    static const QColor kPresetColors[] = {
        QColor(70, 130, 220),
        QColor(220, 70, 70),
        QColor(80, 180, 90),
        QColor(230, 170, 50),
        QColor(170, 90, 210),
        QColor(230, 110, 170),
        QColor(60, 190, 190),
        QColor(160, 160, 160),
    };

    if (index < 0 || index >= kPresetColorCount) {
        return kDefaultColor;
    }
    return kPresetColors[index];
}

QString EqColorPalette::presetColorLabel(int index)
{
    return QStringLiteral("Label %1").arg(index + 1);
}

EqColorPalette::EqColorPalette(QWidget *parent)
    : QWidget(parent)
    , m_group(new QButtonGroup(this))
{
    m_group->setExclusive(true);

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(4);

    setupPresetColors();

    m_addButton = new QToolButton(this);
    m_addButton->setAutoRaise(true);
    m_addButton->setFixedSize(22, 22);
    m_addButton->setText(QStringLiteral("+"));
    m_addButton->setToolTip(QStringLiteral("Add another color"));
    connect(m_addButton, &QToolButton::clicked, this, &EqColorPalette::showAddColorMenu);

    clearSelection();

    connect(m_group, &QButtonGroup::idClicked, this, [this](int) {
        if (auto *button = qobject_cast<QAbstractButton *>(m_group->checkedButton())) {
            m_selectedColor = button->property("eqColor").value<QColor>();
            emit colorSelected(m_selectedColor);
            emit selectionChanged();
        }
    });
}

void EqColorPalette::setupPresetColors()
{
    m_presetColors.clear();
    m_presetColors.reserve(kPresetColorCount);
    for (int i = 0; i < kPresetColorCount; ++i) {
        m_presetColors.push_back(presetColorAt(i));
    }
}

QToolButton *EqColorPalette::makeColorButton(const QColor &color)
{
    auto *button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setCheckable(true);
    button->setFixedSize(22, 22);
    button->setToolTip(color.name());
    button->setProperty("eqColor", color);
    button->setStyleSheet(QStringLiteral(
        "QToolButton { background-color: %1; border: 2px solid #555; border-radius: 11px; }"
        "QToolButton:checked { border: 2px solid white; }")
                              .arg(color.name()));
    return button;
}

void EqColorPalette::rebuildChips()
{
    const QList<QAbstractButton *> buttons = m_group->buttons();
    for (QAbstractButton *button : buttons) {
        m_group->removeButton(button);
        m_layout->removeWidget(button);
        button->deleteLater();
    }

    for (const QColor &color : m_activeColors) {
        QToolButton *button = makeColorButton(color);
        m_group->addButton(button);
        m_layout->addWidget(button);
        if (color == m_selectedColor) {
            button->setChecked(true);
        }
    }

    if (m_addButton) {
        m_layout->removeWidget(m_addButton);
        m_layout->addWidget(m_addButton);
    }
}

void EqColorPalette::addChip(const QColor &color, bool select)
{
    if (!color.isValid()) {
        return;
    }

    for (const QColor &existing : m_activeColors) {
        if (existing == color) {
            if (select) {
                selectColor(color);
            }
            return;
        }
    }

    m_activeColors.push_back(color);
    rebuildChips();

    if (select) {
        selectColor(color);
    }
}

void EqColorPalette::selectColor(const QColor &color)
{
    m_selectedColor = color;
    for (QAbstractButton *button : m_group->buttons()) {
        const QColor buttonColor = button->property("eqColor").value<QColor>();
        button->setChecked(buttonColor == color);
    }
    emit colorSelected(m_selectedColor);
    emit selectionChanged();
}

void EqColorPalette::showAddColorMenu()
{
    QMenu menu(this);

    for (const QColor &color : m_presetColors) {
        bool alreadyShown = false;
        for (const QColor &active : m_activeColors) {
            if (active == color) {
                alreadyShown = true;
                break;
            }
        }
        if (alreadyShown) {
            continue;
        }

        auto *action = menu.addAction(color.name());
        action->setData(color);
        QPixmap swatch(16, 16);
        swatch.fill(color);
        action->setIcon(QIcon(swatch));
    }

    if (menu.isEmpty()) {
        auto *action = menu.addAction(QStringLiteral("All colors are shown"));
        action->setEnabled(false);
    }

    const QPoint menuPos = m_addButton->mapToGlobal(QPoint(0, m_addButton->height()));
    if (QAction *chosen = menu.exec(menuPos)) {
        const QColor color = chosen->data().value<QColor>();
        if (color.isValid()) {
            addChip(color, true);
        }
    }
}

QColor EqColorPalette::selectedColor() const
{
    return m_selectedColor;
}

bool EqColorPalette::hasSelection() const
{
    return m_selectedColor.isValid();
}

void EqColorPalette::clearSelection()
{
    m_activeColors.clear();
    m_selectedColor = kDefaultColor;
    m_activeColors.push_back(kDefaultColor);
    rebuildChips();
    selectColor(kDefaultColor);
}
