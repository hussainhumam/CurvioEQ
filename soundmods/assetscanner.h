#pragma once

#include "soundmodtypes.h"

#include <functional>

class AssetScanner
{
public:
    using ProgressCallback = std::function<void(int discoveredCount)>;

    SoundScanResult scan(const QString &scanRoot, ProgressCallback progress = {}) const;

private:
    static QString formatLabel(SoundAssetFormat format);
    static bool isSkippablePathComponent(const QString &name);
    static bool pathContainsSkippableComponent(const QString &relativePath);
};
