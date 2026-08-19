#include "formathandler.h"

#include "formathandlers.h"

#include <QFileInfo>

FormatHandlerRegistry::FormatHandlerRegistry()
{
    static LooseAudioHandler looseHandler;
    static FfmpegLooseHandler ffmpegHandler;
    static ExternalContainerHandler wemHandler(SoundAssetFormat::Wem, {QStringLiteral("wem")});
    static ExternalContainerHandler bankHandler(SoundAssetFormat::FmodBank, {QStringLiteral("bank")});
    static ExternalContainerHandler fsbHandler(SoundAssetFormat::FmodFsb, {QStringLiteral("fsb")});
    static ExternalContainerHandler pakHandler(SoundAssetFormat::UnrealPak,
                                               {QStringLiteral("pak"), QStringLiteral("ucas"), QStringLiteral("utoc")});

    m_handlers = {&looseHandler,
                  &ffmpegHandler,
                  &wemHandler,
                  &bankHandler,
                  &fsbHandler,
                  &pakHandler};
}

FormatHandlerRegistry &FormatHandlerRegistry::instance()
{
    static FormatHandlerRegistry registry;
    return registry;
}

void FormatHandlerRegistry::registerHandler(const IFormatHandler *handler)
{
    if (handler) {
        m_handlers.push_back(handler);
    }
}

const IFormatHandler *FormatHandlerRegistry::handlerForPath(const QString &absolutePath) const
{
    const QString extension = QFileInfo(absolutePath).suffix();
    for (const IFormatHandler *handler : m_handlers) {
        if (handler && handler->matchesExtension(extension)) {
            return handler;
        }
    }
    return nullptr;
}

SoundAssetFormat FormatHandlerRegistry::formatForExtension(const QString &extension) const
{
    for (const IFormatHandler *handler : m_handlers) {
        if (handler && handler->matchesExtension(extension)) {
            return handler->format();
        }
    }
    return SoundAssetFormat::Unknown;
}
