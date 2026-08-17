#include "ui/appconstants.h"
#include "ui/appiconprovider.h"
#include "ui/singleinstanceserver.h"

#include "audio/dspstatus.h"
#include "mainwindow.h"

#include "audio/audiopolicyrouter.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <process.h>

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

void attachConsoleForDspStatus()
{
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
    }

    FILE *stdoutFile = nullptr;
    FILE *stderrFile = nullptr;
    freopen_s(&stdoutFile, "CONOUT$", "w", stdout);
    freopen_s(&stderrFile, "CONOUT$", "w", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
}

int runDspStatusVerifier()
{
    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) == 0) {
        return 1;
    }

    const QFileInfo appFile(QString::fromWCharArray(modulePath));
    const QString verifyExe = QDir(appFile.absolutePath())
                                  .filePath(QStringLiteral("CurvioEQ_DspVerify.exe"));
    if (!QFileInfo::exists(verifyExe)) {
        attachConsoleForDspStatus();
        setDspStatusWin32ConsoleOutput(true);
        printDspArchitectureState();
        return runDspVerification();
    }

    const std::wstring verifyPath = verifyExe.toStdWString();
    const int exitCode = _wspawnlp(_P_WAIT, verifyPath.c_str(), verifyPath.c_str(), nullptr, nullptr);
    if (exitCode == -1) {
        attachConsoleForDspStatus();
        setDspStatusWin32ConsoleOutput(true);
        printDspArchitectureState();
        return runDspVerification();
    }
    return exitCode;
}
} // namespace

int main(int argc, char *argv[])
{
    if (hasCommandLineFlag(argc, argv, "--dsp-status")) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        return runDspStatusVerifier();
    }

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
