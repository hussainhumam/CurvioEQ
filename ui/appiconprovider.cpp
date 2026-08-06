#include "appiconprovider.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QJsonObject>
#include <QPixmap>

#include <cstring>

QHash<unsigned long, QIcon> AppIconProvider::s_iconCache;
QHash<unsigned long, QString> AppIconProvider::s_nameCache;

QIcon AppIconProvider::appIcon()
{
    static const QIcon icon(QStringLiteral(":/icons/app_icon.png"));
    return icon;
}

namespace {

QIcon iconFromHIcon(HICON iconHandle, int size)
{
    if (!iconHandle) {
        return QIcon();
    }

    const HDC screenDc = GetDC(nullptr);
    const HDC memoryDc = CreateCompatibleDC(screenDc);

    BITMAPINFO header = {};
    header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth = size;
    header.bmiHeader.biHeight = -size;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP dib = CreateDIBSection(memoryDc, &header, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memoryDc);
        ReleaseDC(nullptr, screenDc);
        return QIcon();
    }

    const HGDIOBJ oldBitmap = SelectObject(memoryDc, dib);
    DrawIconEx(memoryDc, 0, 0, iconHandle, size, size, 0, nullptr, DI_NORMAL);

    QImage image(size, size, QImage::Format_ARGB32);
    std::memcpy(image.bits(), bits, static_cast<size_t>(size * size * 4));

    SelectObject(memoryDc, oldBitmap);
    DeleteObject(dib);
    DeleteDC(memoryDc);
    ReleaseDC(nullptr, screenDc);

    return QIcon(QPixmap::fromImage(image));
}

QIcon loadIconFromExe(const QString &exePath)
{
    static QFileIconProvider fileIconProvider;
    QIcon icon = fileIconProvider.icon(QFileInfo(exePath));
    if (!icon.isNull()) {
        return icon;
    }

    SHFILEINFOW fileInfo = {};
    if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(exePath.utf16()), 0, &fileInfo,
                       sizeof(fileInfo), SHGFI_ICON | SHGFI_LARGEICON) != 0
        && fileInfo.hIcon) {
        icon = iconFromHIcon(fileInfo.hIcon, 32);
        DestroyIcon(fileInfo.hIcon);
    }

    return icon;
}

} // namespace

QString AppIconProvider::executablePathForProcess(unsigned long processId)
{
    if (processId == 0) {
        return {};
    }

    HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!processHandle) {
        return {};
    }

    wchar_t exePath[MAX_PATH] = {};
    DWORD pathLength = MAX_PATH;
    if (!QueryFullProcessImageNameW(processHandle, 0, exePath, &pathLength)) {
        CloseHandle(processHandle);
        return {};
    }
    CloseHandle(processHandle);

    return QString::fromWCharArray(exePath);
}

QString AppIconProvider::displayNameForProcess(unsigned long processId)
{
    if (processId == 0) {
        return {};
    }

    const auto cached = s_nameCache.constFind(processId);
    if (cached != s_nameCache.constEnd()) {
        return cached.value();
    }

    const QString exePath = executablePathForProcess(processId);
    if (exePath.isEmpty()) {
        return {};
    }

    const QString name = QFileInfo(exePath).completeBaseName();
    s_nameCache.insert(processId, name);
    return name;
}

QIcon AppIconProvider::iconForProcess(unsigned long processId)
{
    if (processId == 0) {
        return QIcon();
    }

    const auto cached = s_iconCache.constFind(processId);
    if (cached != s_iconCache.constEnd()) {
        return cached.value();
    }

    const QString exePath = executablePathForProcess(processId);
    if (exePath.isEmpty()) {
        return QIcon();
    }

    const QIcon icon = loadIconFromExe(exePath);

    if (icon.isNull()) {
        return QIcon();
    }

    s_iconCache.insert(processId, icon);
    return icon;
}
