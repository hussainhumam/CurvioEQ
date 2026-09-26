#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audioclient.h>

inline bool wasapiDeviceLost(HRESULT hr)
{
    return hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_DEVICE_IN_USE
           || hr == AUDCLNT_E_SERVICE_NOT_RUNNING || hr == AUDCLNT_E_ENDPOINT_CREATE_FAILED;
}
