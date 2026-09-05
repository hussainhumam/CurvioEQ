#pragma once

#include <QString>

class QMenu;
class QWidget;
class Vst3AddonManager;

void populateAddonsMenu(QMenu *menu,
                        Vst3AddonManager *manager,
                        unsigned long processId,
                        const QString &exePath,
                        QWidget *editorParent);
