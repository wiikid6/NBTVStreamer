#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>

// Callback: raw BGR24 pixels, width, height, stride.
using FrameCallback = std::function<void(const uint8_t*, int, int, int)>;

class VideoCapture
{
public:
    VideoCapture();
    ~VideoCapture();

    // Fill names with friendly names of all video capture devices.
    static HRESULT EnumerateDevices(std::vector<std::wstring>& names);

    // Activate device at index and prepare the source reader.
    HRESULT Initialize(int deviceIndex, FrameCallback cb);

    // Start the capture thread.
    void Start();

    // Signal the capture thread to stop and wait for it.
    void Stop();

    // Release all MF resources.
    void Release();

private:
    void CaptureLoop();

    IMFSourceReader*    m_pReader   = nullptr;
    FrameCallback       m_callback;
    std::thread         m_thread;
    std::atomic<bool>   m_running   { false };
};
