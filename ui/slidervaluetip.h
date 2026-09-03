#pragma once

#include <QCursor>
#include <QSlider>
#include <QString>
#include <QToolTip>

#include <functional>
#include <utility>

inline QString formatSignedDb(int value)
{
    if (value > 0) {
        return QStringLiteral("+%1 dB").arg(value);
    }
    return QStringLiteral("%1 dB").arg(value);
}

inline void installSliderValueTip(QSlider *slider, std::function<QString(int)> format)
{
    if (!slider || !format) {
        return;
    }

    auto update = [slider, format = std::move(format)](int value) {
        const QString text = format(value);
        slider->setToolTip(text);
        if (slider->isSliderDown() || slider->underMouse()) {
            QToolTip::showText(QCursor::pos(), text, slider);
        }
    };

    QObject::connect(slider, &QSlider::valueChanged, slider, update);
    QObject::connect(slider, &QSlider::sliderPressed, slider, [slider, update]() {
        update(slider->value());
    });
    update(slider->value());
}
