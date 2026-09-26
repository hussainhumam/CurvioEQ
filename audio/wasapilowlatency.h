#pragma once

#include <audioclient.h>

#include <algorithm>

struct SharedModeEnginePeriod {
    bool ok = false;
    UINT32 defaultPeriod = 0;
    UINT32 fundamentalPeriod = 0;
    UINT32 minPeriod = 0;
    UINT32 maxPeriod = 0;
};

inline SharedModeEnginePeriod querySharedModeEnginePeriod(IAudioClient *client, const WAVEFORMATEX *format)
{
    SharedModeEnginePeriod result;
    if (!client || !format) {
        return result;
    }

    IAudioClient3 *client3 = nullptr;
    if (FAILED(client->QueryInterface(__uuidof(IAudioClient3), reinterpret_cast<void **>(&client3))) || !client3) {
        return result;
    }

    HRESULT hr = client3->GetSharedModeEnginePeriod(const_cast<WAVEFORMATEX *>(format),
                                                    &result.defaultPeriod,
                                                    &result.fundamentalPeriod,
                                                    &result.minPeriod,
                                                    &result.maxPeriod);
    client3->Release();
    result.ok = SUCCEEDED(hr) && result.minPeriod > 0;
    return result;
}

inline UINT32 snapEnginePeriod(const SharedModeEnginePeriod &engine, UINT32 requested)
{
    if (!engine.ok) {
        return requested;
    }

    UINT32 candidate = requested > 0 ? requested : engine.defaultPeriod;
    if (candidate == 0) {
        candidate = engine.defaultPeriod;
    }
    candidate = std::max(candidate, engine.minPeriod);
    if (engine.maxPeriod > 0) {
        candidate = std::min(candidate, engine.maxPeriod);
    }
    if (engine.fundamentalPeriod > 0 && candidate >= engine.minPeriod) {
        const UINT32 fund = engine.fundamentalPeriod;
        const UINT32 minPeriod = engine.minPeriod;
        const UINT32 steps = (candidate - minPeriod + fund / 2) / fund;
        candidate = minPeriod + steps * fund;
        if (engine.maxPeriod > 0 && candidate > engine.maxPeriod) {
            candidate = minPeriod + ((engine.maxPeriod - minPeriod) / fund) * fund;
        }
        candidate = std::max(candidate, minPeriod);
    }
    return candidate;
}

inline bool initializeSharedSnappedPeriod(IAudioClient *client,
                                          WAVEFORMATEX *format,
                                          DWORD streamFlags,
                                          UINT32 requestedPeriod,
                                          UINT32 *periodFrames)
{
    if (!client || !format) {
        return false;
    }

    IAudioClient3 *client3 = nullptr;
    if (FAILED(client->QueryInterface(__uuidof(IAudioClient3), reinterpret_cast<void **>(&client3))) || !client3) {
        return false;
    }

    SharedModeEnginePeriod engine;
    HRESULT hr = client3->GetSharedModeEnginePeriod(format,
                                                    &engine.defaultPeriod,
                                                    &engine.fundamentalPeriod,
                                                    &engine.minPeriod,
                                                    &engine.maxPeriod);
    if (FAILED(hr) || engine.minPeriod == 0) {
        client3->Release();
        return false;
    }
    engine.ok = true;

    auto tryPeriod = [&](UINT32 period) -> bool {
        if (period == 0) {
            return false;
        }
        UINT32 candidate = snapEnginePeriod(engine, period);
        const HRESULT initHr = client3->InitializeSharedAudioStream(streamFlags, candidate, format, nullptr);
        if (SUCCEEDED(initHr)) {
            if (periodFrames) {
                *periodFrames = candidate;
            }
            return true;
        }
        return false;
    };

    const UINT32 snapped = snapEnginePeriod(engine, requestedPeriod);
    bool ok = tryPeriod(snapped);
    if (!ok && engine.defaultPeriod != snapped) {
        ok = tryPeriod(engine.defaultPeriod);
    }

    client3->Release();
    return ok;
}

inline bool initializeSharedLowLatency(IAudioClient *client,
                                       WAVEFORMATEX *format,
                                       DWORD streamFlags,
                                       UINT32 *periodFrames)
{
    if (!client || !format) {
        return false;
    }

    IAudioClient3 *client3 = nullptr;
    if (FAILED(client->QueryInterface(__uuidof(IAudioClient3), reinterpret_cast<void **>(&client3))) || !client3) {
        return false;
    }

    UINT32 defaultPeriod = 0;
    UINT32 fundamentalPeriod = 0;
    UINT32 minPeriod = 0;
    UINT32 maxPeriod = 0;
    HRESULT hr = client3->GetSharedModeEnginePeriod(format,
                                                    &defaultPeriod,
                                                    &fundamentalPeriod,
                                                    &minPeriod,
                                                    &maxPeriod);
    if (FAILED(hr) || minPeriod == 0) {
        client3->Release();
        return false;
    }

    auto tryPeriod = [&](UINT32 period) -> bool {
        UINT32 candidate = std::max(period, minPeriod);
        if (maxPeriod > 0) {
            candidate = std::min(candidate, maxPeriod);
        }
        const HRESULT initHr = client3->InitializeSharedAudioStream(streamFlags, candidate, format, nullptr);
        if (SUCCEEDED(initHr)) {
            if (periodFrames) {
                *periodFrames = candidate;
            }
            return true;
        }
        return false;
    };

    bool ok = tryPeriod(minPeriod);
    if (!ok && fundamentalPeriod > 0) {
        ok = tryPeriod(minPeriod + fundamentalPeriod);
    }
    if (!ok && defaultPeriod >= minPeriod) {
        ok = tryPeriod(defaultPeriod);
    }

    client3->Release();
    return ok;
}
