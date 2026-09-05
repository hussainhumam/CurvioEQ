#include "addonspanel.h"

#include "vst3/vst3addonmanager.h"

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QWidget>

void populateAddonsMenu(QMenu *menu,
                        Vst3AddonManager *manager,
                        unsigned long processId,
                        const QString &exePath,
                        QWidget *editorParent)
{
    if (!menu) {
        return;
    }

    menu->clear();
    menu->setToolTipsVisible(true);

    if (!manager) {
        QAction *unavailable = menu->addAction(QStringLiteral("Add-ons unavailable"));
        unavailable->setEnabled(false);
        return;
    }

    if (processId == 0 || exePath.isEmpty()) {
        QAction *hint = menu->addAction(QStringLiteral("Pick a running app first"));
        hint->setEnabled(false);
        return;
    }

    const Vst3AppAddons addons = manager->addonsForExe(exePath);
    bool hasPlugins = false;
    for (int slot = 0; slot < kAudioChainAddonCount; ++slot) {
        const Vst3SlotState &state = addons.pluginSlots[static_cast<size_t>(slot)];
        if (!state.occupied) {
            continue;
        }
        hasPlugins = true;
        const QString name = state.name.isEmpty() ? state.uid : state.name;
        QMenu *pluginMenu = menu->addMenu(name);

        QAction *configure = pluginMenu->addAction(QStringLiteral("Configure"));
        QObject::connect(configure, &QAction::triggered, editorParent, [manager, processId, slot, editorParent]() {
            manager->openEditor(processId, slot, editorParent);
        });

        QAction *remove = pluginMenu->addAction(QStringLiteral("Delete"));
        QObject::connect(remove, &QAction::triggered, editorParent, [manager, processId, exePath, slot]() {
            manager->removePlugin(processId, exePath, slot);
        });
    }

    if (hasPlugins) {
        menu->addSeparator();
    }

    QMenu *addMenu = menu->addMenu(QStringLiteral("Add"));
    if (addons.firstFreeSlot() < 0) {
        addMenu->setEnabled(false);
        addMenu->setToolTip(QStringLiteral("This app already has 4 plugins"));
        return;
    }

    const QVector<Vst3PluginInfo> plugins = manager->installedPlugins();
    if (plugins.isEmpty()) {
        QAction *empty = addMenu->addAction(QStringLiteral("No VST3 plugins found"));
        empty->setEnabled(false);
        return;
    }

    for (const Vst3PluginInfo &info : plugins) {
        QString label = info.name;
        if (!info.vendor.isEmpty()) {
            label += QStringLiteral("  ·  %1").arg(info.vendor);
        }
        QAction *action = addMenu->addAction(label);
        QObject::connect(action, &QAction::triggered, editorParent, [manager, processId, exePath, info]() {
            manager->addPlugin(processId, exePath, info, nullptr);
        });
    }
}
