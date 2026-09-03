#pragma once

#include <QString>
#include <Qt>

class QKeyEvent;

class HotkeySequence
{
public:
    quint32 scanCode = 0;
    bool extended = false;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    bool isEmpty() const;
    bool isValid() const;
    bool hasModifier() const;

    bool operator==(const HotkeySequence &other) const;
    bool operator!=(const HotkeySequence &other) const;

    static Qt::KeyboardModifiers hotkeyModifiers(Qt::KeyboardModifiers modifiers);
    static quintptr layoutToken();

    static HotkeySequence fromKeyEvent(const QKeyEvent *event);
    static HotkeySequence fromStoredString(const QString &text);

    QString toStoredString() const;
    QString displayString() const;
    bool toNative(quint32 *modifiers, quint32 *virtualKey) const;

private:
    static bool isModifierKey(int qtKey);
};
