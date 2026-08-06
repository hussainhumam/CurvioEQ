#pragma once

#include <QString>
#include <QDebug>

namespace AudioLog {

inline void info(const QString &tag, const QString &message)
{
    qInfo().noquote() << QStringLiteral("[%1] %2").arg(tag, message);
}

inline void warn(const QString &tag, const QString &message)
{
    qWarning().noquote() << QStringLiteral("[%1] %2").arg(tag, message);
}

inline void error(const QString &tag, const QString &message)
{
    qCritical().noquote() << QStringLiteral("[%1] %2").arg(tag, message);
}

inline QString hresultToString(long hr)
{
    return QStringLiteral("HRESULT=0x%1").arg(static_cast<ulong>(hr), 8, 16, QChar('0'));
}

inline void logHresult(const QString &tag, const QString &step, long hr)
{
    if (hr < 0) {
        error(tag, QStringLiteral("%1 failed: %2").arg(step, hresultToString(hr)));
    } else {
        info(tag, QStringLiteral("%1 ok").arg(step));
    }
}

} // namespace AudioLog
