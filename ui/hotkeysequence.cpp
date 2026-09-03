#include "hotkeysequence.h"

#include <QKeyEvent>
#include <QKeySequence>
#include <QStringList>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

constexpr Qt::KeyboardModifiers kHotkeyModifierMask =
    Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

UINT qtKeyToVirtualKey(int qtKey)
{
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        return static_cast<UINT>('A' + (qtKey - Qt::Key_A));
    }
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        return static_cast<UINT>('0' + (qtKey - Qt::Key_0));
    }
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        return static_cast<UINT>(VK_F1 + (qtKey - Qt::Key_F1));
    }

    switch (qtKey) {
    case Qt::Key_Space:
        return VK_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return VK_RETURN;
    case Qt::Key_Escape:
        return VK_ESCAPE;
    case Qt::Key_Tab:
        return VK_TAB;
    case Qt::Key_Backspace:
        return VK_BACK;
    case Qt::Key_Delete:
        return VK_DELETE;
    case Qt::Key_Insert:
        return VK_INSERT;
    case Qt::Key_Home:
        return VK_HOME;
    case Qt::Key_End:
        return VK_END;
    case Qt::Key_PageUp:
        return VK_PRIOR;
    case Qt::Key_PageDown:
        return VK_NEXT;
    case Qt::Key_Left:
        return VK_LEFT;
    case Qt::Key_Right:
        return VK_RIGHT;
    case Qt::Key_Up:
        return VK_UP;
    case Qt::Key_Down:
        return VK_DOWN;
    case Qt::Key_Pause:
        return VK_PAUSE;
    case Qt::Key_Print:
        return VK_SNAPSHOT;
    case Qt::Key_CapsLock:
        return VK_CAPITAL;
    case Qt::Key_NumLock:
        return VK_NUMLOCK;
    case Qt::Key_ScrollLock:
        return VK_SCROLL;
    case Qt::Key_Comma:
        return VK_OEM_COMMA;
    case Qt::Key_Period:
        return VK_OEM_PERIOD;
    case Qt::Key_Slash:
        return VK_OEM_2;
    case Qt::Key_Semicolon:
        return VK_OEM_1;
    case Qt::Key_Apostrophe:
        return VK_OEM_7;
    case Qt::Key_BracketLeft:
        return VK_OEM_4;
    case Qt::Key_BracketRight:
        return VK_OEM_6;
    case Qt::Key_Backslash:
        return VK_OEM_5;
    case Qt::Key_Minus:
        return VK_OEM_MINUS;
    case Qt::Key_Equal:
        return VK_OEM_PLUS;
    case Qt::Key_QuoteLeft:
        return VK_OEM_3;
    case Qt::Key_Plus:
        return VK_OEM_PLUS;
    case Qt::Key_Asterisk:
        return VK_MULTIPLY;
    case Qt::Key_MediaPlay:
        return VK_MEDIA_PLAY_PAUSE;
    case Qt::Key_MediaStop:
        return VK_MEDIA_STOP;
    case Qt::Key_MediaNext:
        return VK_MEDIA_NEXT_TRACK;
    case Qt::Key_MediaPrevious:
        return VK_MEDIA_PREV_TRACK;
    case Qt::Key_VolumeUp:
        return VK_VOLUME_UP;
    case Qt::Key_VolumeDown:
        return VK_VOLUME_DOWN;
    case Qt::Key_VolumeMute:
        return VK_VOLUME_MUTE;
    default:
        return 0;
    }
}

bool isExtendedVirtualKey(UINT virtualKey)
{
    switch (virtualKey) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_SNAPSHOT:
        return true;
    default:
        return false;
    }
}

UINT packedScanCode(quint32 scanCode, bool extended)
{
    UINT value = scanCode & 0xFFu;
    if (extended) {
        value |= 0xE000u;
    }
    return value;
}

HKL foregroundKeyboardLayout()
{
    const HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        const DWORD threadId = GetWindowThreadProcessId(hwnd, nullptr);
        if (threadId != 0) {
            const HKL layout = GetKeyboardLayout(threadId);
            if (layout) {
                return layout;
            }
        }
    }
    return GetKeyboardLayout(0);
}

HotkeySequence fromVirtualKey(UINT virtualKey, Qt::KeyboardModifiers modifiers, bool extendedHint)
{
    HotkeySequence sequence;
    if (virtualKey == 0) {
        return sequence;
    }

    const UINT mapped = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC_EX);
    sequence.scanCode = mapped & 0xFFu;
    sequence.extended = extendedHint || ((mapped & 0xFF00u) != 0) || isExtendedVirtualKey(virtualKey);
    sequence.modifiers = HotkeySequence::hotkeyModifiers(modifiers);
    if (sequence.scanCode == 0) {
        sequence.scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC) & 0xFFu;
    }
    return sequence;
}

QString modifierString(Qt::KeyboardModifiers modifiers)
{
    QStringList parts;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        parts.append(QStringLiteral("Ctrl"));
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        parts.append(QStringLiteral("Alt"));
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        parts.append(QStringLiteral("Shift"));
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        parts.append(QStringLiteral("Win"));
    }
    return parts.join(QLatin1Char('+'));
}

Qt::KeyboardModifiers modifiersFromString(const QString &text)
{
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    const QStringList parts = text.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.compare(QStringLiteral("Ctrl"), Qt::CaseInsensitive) == 0
            || part.compare(QStringLiteral("Control"), Qt::CaseInsensitive) == 0) {
            modifiers |= Qt::ControlModifier;
        } else if (part.compare(QStringLiteral("Alt"), Qt::CaseInsensitive) == 0) {
            modifiers |= Qt::AltModifier;
        } else if (part.compare(QStringLiteral("Shift"), Qt::CaseInsensitive) == 0) {
            modifiers |= Qt::ShiftModifier;
        } else if (part.compare(QStringLiteral("Win"), Qt::CaseInsensitive) == 0
                   || part.compare(QStringLiteral("Meta"), Qt::CaseInsensitive) == 0
                   || part.compare(QStringLiteral("Super"), Qt::CaseInsensitive) == 0) {
            modifiers |= Qt::MetaModifier;
        }
    }
    return modifiers;
}

} // namespace

bool HotkeySequence::isEmpty() const
{
    return scanCode == 0;
}

bool HotkeySequence::isValid() const
{
    return scanCode != 0;
}

bool HotkeySequence::hasModifier() const
{
    return hotkeyModifiers(modifiers) != Qt::NoModifier;
}

bool HotkeySequence::operator==(const HotkeySequence &other) const
{
    return scanCode == other.scanCode && extended == other.extended
           && hotkeyModifiers(modifiers) == hotkeyModifiers(other.modifiers);
}

bool HotkeySequence::operator!=(const HotkeySequence &other) const
{
    return !(*this == other);
}

Qt::KeyboardModifiers HotkeySequence::hotkeyModifiers(Qt::KeyboardModifiers modifiers)
{
    return modifiers & kHotkeyModifierMask;
}

quintptr HotkeySequence::layoutToken()
{
    return reinterpret_cast<quintptr>(foregroundKeyboardLayout());
}

bool HotkeySequence::isModifierKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return true;
    default:
        return false;
    }
}

HotkeySequence HotkeySequence::fromKeyEvent(const QKeyEvent *event)
{
    HotkeySequence sequence;
    if (!event || event->isAutoRepeat() || isModifierKey(event->key())) {
        return sequence;
    }

    const quint32 nativeScan = event->nativeScanCode();
    sequence.scanCode = nativeScan & 0xFFu;
    sequence.extended = (nativeScan & 0x100u) != 0;
    sequence.modifiers = hotkeyModifiers(event->modifiers());

    const UINT nativeVk = static_cast<UINT>(event->nativeVirtualKey());
    if (sequence.scanCode == 0 && nativeVk != 0) {
        sequence = fromVirtualKey(nativeVk, sequence.modifiers, sequence.extended);
        sequence.modifiers = hotkeyModifiers(event->modifiers());
    } else if (isExtendedVirtualKey(nativeVk)) {
        sequence.extended = true;
    }

    return sequence;
}

HotkeySequence HotkeySequence::fromStoredString(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    if (trimmed.startsWith(QStringLiteral("sc:"))) {
        HotkeySequence sequence;
        const QStringList parts = trimmed.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const int split = part.indexOf(QLatin1Char(':'));
            if (split <= 0) {
                continue;
            }
            const QString key = part.left(split);
            const QString value = part.mid(split + 1);
            if (key == QStringLiteral("sc")) {
                bool ok = false;
                sequence.scanCode = value.toUInt(&ok, 0);
                if (!ok) {
                    sequence.scanCode = 0;
                }
            } else if (key == QStringLiteral("ext")) {
                sequence.extended = (value == QStringLiteral("1") || value.compare(QStringLiteral("true"),
                                                                                   Qt::CaseInsensitive)
                                                                       == 0);
            } else if (key == QStringLiteral("mod")) {
                sequence.modifiers = modifiersFromString(value);
            }
        }
        return sequence;
    }

    const QKeySequence keySequence(trimmed, QKeySequence::PortableText);
    if (keySequence.isEmpty()) {
        return {};
    }

    const QKeyCombination combo = keySequence[0];
    const UINT virtualKey = qtKeyToVirtualKey(combo.key());
    if (virtualKey == 0) {
        return {};
    }

    const bool extended = combo.key() == Qt::Key_Insert || combo.key() == Qt::Key_Delete
                          || combo.key() == Qt::Key_Home || combo.key() == Qt::Key_End
                          || combo.key() == Qt::Key_PageUp || combo.key() == Qt::Key_PageDown
                          || combo.key() == Qt::Key_Left || combo.key() == Qt::Key_Right
                          || combo.key() == Qt::Key_Up || combo.key() == Qt::Key_Down
                          || combo.key() == Qt::Key_Enter;
    return fromVirtualKey(virtualKey, combo.keyboardModifiers(), extended);
}

QString HotkeySequence::toStoredString() const
{
    if (!isValid()) {
        return {};
    }

    QString text = QStringLiteral("sc:0x%1").arg(scanCode, 0, 16);
    if (extended) {
        text += QStringLiteral(";ext:1");
    }
    const QString mods = modifierString(hotkeyModifiers(modifiers));
    if (!mods.isEmpty()) {
        text += QStringLiteral(";mod:%1").arg(mods);
    }
    return text;
}

QString HotkeySequence::displayString() const
{
    if (!isValid()) {
        return {};
    }

    LONG lParam = static_cast<LONG>((scanCode & 0xFFu) << 16);
    if (extended) {
        lParam |= 1 << 24;
    }

    wchar_t name[128] = {};
    const int length = GetKeyNameTextW(lParam, name, 128);
    QString keyName;
    if (length > 0) {
        keyName = QString::fromWCharArray(name, length);
    } else {
        keyName = QStringLiteral("0x%1").arg(scanCode, 2, 16, QLatin1Char('0'));
    }

    const QString mods = modifierString(hotkeyModifiers(modifiers));
    if (mods.isEmpty()) {
        return keyName;
    }
    return QStringLiteral("%1+%2").arg(mods, keyName);
}

bool HotkeySequence::toNative(quint32 *modifiers, quint32 *virtualKey) const
{
    if (!isValid() || !modifiers || !virtualKey) {
        return false;
    }

    const UINT mapped = MapVirtualKeyExW(packedScanCode(scanCode, extended), MAPVK_VSC_TO_VK_EX,
                                         foregroundKeyboardLayout());
    UINT vk = mapped;
    if (vk == 0) {
        vk = MapVirtualKeyW(packedScanCode(scanCode, extended), MAPVK_VSC_TO_VK_EX);
    }
    if (vk == 0) {
        vk = MapVirtualKeyW(scanCode & 0xFFu, MAPVK_VSC_TO_VK);
    }
    if (vk == 0 || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN
        || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_LMENU
        || vk == VK_RMENU) {
        return false;
    }

    UINT nativeModifiers = 0;
    if (this->modifiers.testFlag(Qt::ShiftModifier)) {
        nativeModifiers |= MOD_SHIFT;
    }
    if (this->modifiers.testFlag(Qt::ControlModifier)) {
        nativeModifiers |= MOD_CONTROL;
    }
    if (this->modifiers.testFlag(Qt::AltModifier)) {
        nativeModifiers |= MOD_ALT;
    }
    if (this->modifiers.testFlag(Qt::MetaModifier)) {
        nativeModifiers |= MOD_WIN;
    }

    *modifiers = nativeModifiers;
    *virtualKey = vk;
    return true;
}
