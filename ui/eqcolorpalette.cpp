#include "eqcolorpalette.h"

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
