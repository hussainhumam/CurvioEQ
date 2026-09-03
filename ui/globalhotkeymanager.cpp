#include "globalhotkeymanager.h"

#include "hotkeysequence.h"

#include <QApplication>
#include <QTimer>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

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

    m_layoutTimer = new QTimer(this);
    m_layoutTimer->setInterval(400);
    connect(m_layoutTimer, &QTimer::timeout, this, &GlobalHotkeyManager::reregisterIfLayoutChanged);
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
    unregisterAll();
    m_settings = settings;
    m_windowId = windowId;
    m_registeredLayout = 0;

    if (!settings.keybindsEnabled || windowId == 0) {
        m_layoutTimer->stop();
        return;
    }

    registerAll();
    m_registeredLayout = HotkeySequence::layoutToken();
    m_layoutTimer->start();
}

void GlobalHotkeyManager::clear()
{
    unregisterAll();
    m_layoutTimer->stop();
    m_windowId = 0;
    m_registeredLayout = 0;
}

void GlobalHotkeyManager::registerAll()
{
    if (!m_settings.keybindsEnabled || m_windowId == 0) {
        return;
    }

    registerSequence(kEqToggleHotkeyId,
                     m_settings.eqToggleKeybind,
                     m_windowId,
                     QStringLiteral("EQ disable all"));
    registerSequence(kOutputMuteHotkeyId,
                     m_settings.outputMuteKeybind,
                     m_windowId,
                     QStringLiteral("Mute output"));

    for (int colorIndex = 0; colorIndex < AppSettings::kEqColorKeybindCount; ++colorIndex) {
        registerSequence(eqColorHotkeyId(colorIndex),
                         m_settings.eqColorKeybinds[static_cast<size_t>(colorIndex)],
                         m_windowId,
                         QStringLiteral("Mute EQ label %1").arg(colorIndex + 1));
    }
}

bool GlobalHotkeyManager::registerSequence(int hotkeyId,
                                           const QString &sequenceText,
                                           WId windowId,
                                           const QString &label)
{
    if (sequenceText.trimmed().isEmpty()) {
        return true;
    }

    const HotkeySequence sequence = HotkeySequence::fromStoredString(sequenceText);
    if (!sequence.isValid()) {
        emit registrationFailed(QStringLiteral("%1 keybind is invalid").arg(label));
        return false;
    }

    quint32 modifiers = 0;
    quint32 virtualKey = 0;
    if (!sequence.toNative(&modifiers, &virtualKey)) {
        emit registrationFailed(QStringLiteral("%1 keybind uses an unsupported key").arg(label));
        return false;
    }

    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    if (!RegisterHotKey(hwnd, static_cast<int>(hotkeyId), modifiers | MOD_NOREPEAT, virtualKey)) {
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

void GlobalHotkeyManager::reregisterIfLayoutChanged()
{
    if (!m_settings.keybindsEnabled || m_windowId == 0) {
        return;
    }

    const quintptr layout = HotkeySequence::layoutToken();
    if (layout == m_registeredLayout) {
        return;
    }

    unregisterAll();
    registerAll();
    m_registeredLayout = layout;
}

bool GlobalHotkeyManager::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)

    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") {
        return false;
    }

    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message == WM_INPUTLANGCHANGE || msg->message == WM_INPUTLANGCHANGEREQUEST) {
        reregisterIfLayoutChanged();
        return false;
    }

    if (msg->message != WM_HOTKEY) {
        return false;
    }

    emit hotkeyTriggered(static_cast<int>(msg->wParam));
    return true;
}
