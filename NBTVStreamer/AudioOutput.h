#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "AudioQueue.h"

class AudioOutput
{
public:
    AudioOutput();
    ~AudioOutput();

    // Fill names with friendly names of active render endpoints.
    static HRESULT EnumerateDevices(std::vector<std::wstring>& names);

    // Prepare the chosen render device.
    HRESULT Initialize(int deviceIndex, std::shared_ptr<AudioQueue> queue);

    // Start the render thread.
    void Start();

    // Signal stop and wait for the render thread to finish.
    void Stop();

    // Release all WASAPI resources.
    void Release();

    // Sample rate negotiated with the device (set after Initialize).
    int GetSampleRate() const { return m_sampleRate; }

private:
    void RenderLoop();

    IMMDevice*          m_pDevice       = nullptr;
    IAudioClient*       m_pAudioClient  = nullptr;
    IAudioRenderClient* m_pRenderClient = nullptr;
    HANDLE              m_hEvent        = nullptr;
    WAVEFORMATEX*       m_pFormat       = nullptr;
    std::shared_ptr<AudioQueue> m_queue;

    int                 m_sampleRate    = 44100;
    int                 m_channels      = 2;
    bool                m_isFloat       = true;

    std::thread         m_thread;
    std::atomic<bool>   m_running { false };
};
