#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QToolButton;

class EqColorPalette : public QWidget
{
    Q_OBJECT

public:
    static constexpr int kPresetColorCount = 8;

    static QColor presetColorAt(int index);
    static QString presetColorLabel(int index);

    explicit EqColorPalette(QWidget *parent = nullptr);

    QColor selectedColor() const;
    bool hasSelection() const;

public slots:
    void clearSelection();

signals:
    void colorSelected(const QColor &color);
    void selectionChanged();

private:
    void setupPresetColors();
    void rebuildChips();
    void addChip(const QColor &color, bool select);
    QToolButton *makeColorButton(const QColor &color);
    void showAddColorMenu();
    void selectColor(const QColor &color);

    QHBoxLayout *m_layout = nullptr;
    QButtonGroup *m_group = nullptr;
    QToolButton *m_addButton = nullptr;
    QVector<QColor> m_presetColors;
    QVector<QColor> m_activeColors;
    QColor m_selectedColor;
};
