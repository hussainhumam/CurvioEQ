#pragma once

#include "soundmodtypes.h"

#include <functional>
#include <vector>

class IFormatHandler
{
public:
    virtual ~IFormatHandler() = default;

    virtual SoundAssetFormat format() const = 0;
    virtual bool matchesExtension(const QString &extension) const = 0;
    virtual SoundPatchSupport patchSupport() const = 0;
    virtual bool applyGain(const QString &sourcePath, const QString &stagingPath, float gainDb, QString *errorMessage) const = 0;
};

using FormatHandlerList = std::vector<const IFormatHandler *>;

class FormatHandlerRegistry
{
public:
    static FormatHandlerRegistry &instance();

    const IFormatHandler *handlerForPath(const QString &absolutePath) const;

private:
    FormatHandlerRegistry();
    std::vector<const IFormatHandler *> m_handlers;
};
