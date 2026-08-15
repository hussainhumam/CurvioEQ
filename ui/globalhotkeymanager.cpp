#include "globalhotkeymanager.h"

#include <QApplication>
#include <QKeySequence>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

bool sequenceToNative(const QKeySequence &sequence, UINT *modifiers, UINT *virtualKey)
{
    if (sequence.isEmpty()) {
        return false;
    }

    const QKeyCombination combo = sequence[0];
    const Qt::KeyboardModifiers qtModifiers = combo.keyboardModifiers();
    const int qtKey = combo.key();

    UINT nativeModifiers = 0;
    if (qtModifiers.testFlag(Qt::ShiftModifier)) {
        nativeModifiers |= MOD_SHIFT;
    }
    if (qtModifiers.testFlag(Qt::ControlModifier)) {
        nativeModifiers |= MOD_CONTROL;
    }
    if (qtModifiers.testFlag(Qt::AltModifier)) {
        nativeModifiers |= MOD_ALT;
    }
    if (qtModifiers.testFlag(Qt::MetaModifier)) {
        nativeModifiers |= MOD_WIN;
    }

    UINT vk = 0;
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        vk = static_cast<UINT>('A' + (qtKey - Qt::Key_A));
    } else if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        vk = static_cast<UINT>('0' + (qtKey - Qt::Key_0));
    } else if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) {
        vk = static_cast<UINT>(VK_F1 + (qtKey - Qt::Key_F1));
    } else {
        switch (qtKey) {
        case Qt::Key_Space:
            vk = VK_SPACE;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            vk = VK_RETURN;
            break;
        case Qt::Key_Escape:
            vk = VK_ESCAPE;
            break;
        case Qt::Key_Tab:
            vk = VK_TAB;
            break;
        case Qt::Key_Backspace:
            vk = VK_BACK;
            break;
        case Qt::Key_Delete:
            vk = VK_DELETE;
            break;
        case Qt::Key_Insert:
            vk = VK_INSERT;
            break;
        case Qt::Key_Home:
            vk = VK_HOME;
            break;
        case Qt::Key_End:
            vk = VK_END;
            break;
        case Qt::Key_PageUp:
            vk = VK_PRIOR;
            break;
        case Qt::Key_PageDown:
            vk = VK_NEXT;
            break;
        case Qt::Key_Left:
            vk = VK_LEFT;
            break;
        case Qt::Key_Right:
            vk = VK_RIGHT;
            break;
        case Qt::Key_Up:
            vk = VK_UP;
            break;
        case Qt::Key_Down:
            vk = VK_DOWN;
            break;
        default:
            return false;
        }
    }

    *modifiers = nativeModifiers;
    *virtualKey = vk;
    return true;
}

} // namespace

int GlobalHotkeyManager::eqColorHotkeyId(int colorIndex)
{
    return kEqColorKeybindBaseId + colorIndex;
}

int GlobalHotkeyManager::eqColorIndexFromHotkeyId(int hotkeyId)
{
    if (hotkeyId < kEqColorKeybindBaseId || hotkeyId > kMaxHotkeyId) {
        return -1;
    }
    return hotkeyId - kEqColorKeybindBaseId;
}

GlobalHotkeyManager::GlobalHotkeyManager(QObject *parent)
    : QObject(parent)
{
    QApplication::instance()->installNativeEventFilter(this);
    m_filterInstalled = true;
}

GlobalHotkeyManager::~GlobalHotkeyManager()
{
    clear();
    if (m_filterInstalled) {
        QApplication::instance()->removeNativeEventFilter(this);
        m_filterInstalled = false;
    }
}

void GlobalHotkeyManager::apply(const AppSettings &settings, WId windowId)
{
    clear();
    m_windowId = windowId;

    if (!settings.keybindsEnabled || windowId == 0) {
        return;
    }

    registerSequence(kEqToggleHotkeyId,
                     settings.eqToggleKeybind,
                     windowId,
                     QStringLiteral("EQ disable all"));
    registerSequence(kOutputMuteHotkeyId,
                     settings.outputMuteKeybind,
                     windowId,
                     QStringLiteral("Mute output"));

    for (int colorIndex = 0; colorIndex < AppSettings::kEqColorKeybindCount; ++colorIndex) {
        registerSequence(eqColorHotkeyId(colorIndex),
                         settings.eqColorKeybinds[static_cast<size_t>(colorIndex)],
                         windowId,
                         QStringLiteral("Mute EQ label %1").arg(colorIndex + 1));
    }
}

void GlobalHotkeyManager::clear()
{
    unregisterAll();
    m_windowId = 0;
}

bool GlobalHotkeyManager::registerSequence(int hotkeyId,
                                           const QString &sequenceText,
                                           WId windowId,
                                           const QString &label)
{
    if (sequenceText.trimmed().isEmpty()) {
        return true;
    }

    const QKeySequence sequence(sequenceText);
    if (sequence.isEmpty()) {
        emit registrationFailed(QStringLiteral("%1 keybind is invalid").arg(label));
        return false;
    }

    UINT modifiers = 0;
    UINT virtualKey = 0;
    if (!sequenceToNative(sequence, &modifiers, &virtualKey)) {
        emit registrationFailed(QStringLiteral("%1 keybind uses an unsupported key").arg(label));
        return false;
    }

    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    if (!RegisterHotKey(hwnd, static_cast<int>(hotkeyId), modifiers, virtualKey)) {
        emit registrationFailed(QStringLiteral("%1 keybind could not be registered (already in use?)")
                                    .arg(label));
        return false;
    }

    return true;
}

void GlobalHotkeyManager::unregisterAll()
{
    if (m_windowId == 0) {
        return;
    }

    const HWND hwnd = reinterpret_cast<HWND>(m_windowId);
    for (int hotkeyId = kEqToggleHotkeyId; hotkeyId <= kMaxHotkeyId; ++hotkeyId) {
        UnregisterHotKey(hwnd, hotkeyId);
    }
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)

    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }

    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_HOTKEY) {
        return false;
    }

    emit hotkeyTriggered(static_cast<int>(msg->wParam));
    return true;
}
