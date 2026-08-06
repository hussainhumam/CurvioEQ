#include "ui/appconstants.h"
#include "ui/appiconprovider.h"
#include "ui/singleinstanceserver.h"

#include "mainwindow.h"

#include "audio/audiopolicyrouter.h"

#include <QApplication>
#include <QCoreApplication>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>

namespace {
bool hasCommandLineFlag(int argc, char *argv[], const char *flag)
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QString::fromLatin1(flag)) {
            return true;
        }
    }
    return false;
}
} // namespace

int main(int argc, char *argv[])
{
    if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--clear-all-routing")) {
        QCoreApplication a(argc, argv);

        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comInitialized = SUCCEEDED(comHr);

        QString errorMessage;
        const bool cleared = AudioPolicyRouter::clearAllPersistedRouting(&errorMessage);

        if (comInitialized) {
            CoUninitialize();
        }
        return cleared ? 0 : 1;
    }

    if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--test-route")) {
        QCoreApplication a(argc, argv);
        const unsigned long processId = QString::fromLocal8Bit(argv[2]).toULong();
        const QString deviceId = QString::fromLocal8Bit(argv[3]);

        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool comInitialized = SUCCEEDED(comHr);

        QString errorMessage;
        const bool routed = AudioPolicyRouter::routeProcessToDevice(processId, deviceId, &errorMessage);

        if (comInitialized) {
            CoUninitialize();
        }
        return routed ? 0 : 1;
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QString::fromLatin1(AppConstants::kAppId));
    QCoreApplication::setApplicationName(QString::fromLatin1(AppConstants::kAppId));
    QApplication::setWindowIcon(AppIconProvider::appIcon());
    QApplication::setQuitOnLastWindowClosed(false);

    const bool startedAtLogin = hasCommandLineFlag(argc, argv, "--startup");

    if (SingleInstanceServer::notifyExistingInstance()) {
        return 0;
    }

    MainWindow w;
    if (startedAtLogin) {
        w.hide();
    } else {
        w.show();
        w.raise();
        w.activateWindow();
    }
    return QApplication::exec();
}
