#pragma once

#include <QColor>
#include <QString>

class EqColorPalette
{
public:
    static constexpr int kPresetColorCount = 8;

    static QColor presetColorAt(int index);
    static QString presetColorLabel(int index);
};
