#pragma once

#include "settingsstore.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QWidget>

class QTimer;

class GlobalHotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    static constexpr int kEqToggleHotkeyId = 1;
    static constexpr int kOutputMuteHotkeyId = 2;
    static constexpr int kEqColorKeybindBaseId = 3;
    static constexpr int kMaxHotkeyId = kEqColorKeybindBaseId + AppSettings::kEqColorKeybindCount - 1;

    static int eqColorHotkeyId(int colorIndex);
    static int eqColorIndexFromHotkeyId(int hotkeyId);

    explicit GlobalHotkeyManager(QObject *parent = nullptr);
    ~GlobalHotkeyManager() override;

    void apply(const AppSettings &settings, WId windowId);
    void clear();

signals:
    void hotkeyTriggered(int hotkeyId);
    void registrationFailed(const QString &description);

protected:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    bool registerSequence(int hotkeyId, const QString &sequenceText, WId windowId, const QString &label);
    void registerAll();
    void unregisterAll();
    void reregisterIfLayoutChanged();

    AppSettings m_settings;
    WId m_windowId = 0;
    quintptr m_registeredLayout = 0;
    bool m_filterInstalled = false;
    QTimer *m_layoutTimer = nullptr;
};
