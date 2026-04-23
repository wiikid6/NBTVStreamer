#include "VideoCapture.h"
#include <wmcodecdsp.h>
#include <stdexcept>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

VideoCapture::VideoCapture() = default;

VideoCapture::~VideoCapture()
{
    Stop();
    Release();
}

HRESULT VideoCapture::EnumerateDevices(std::vector<std::wstring>& names)
{
    names.clear();

    IMFAttributes* pAttr = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttr, 1);
    if (FAILED(hr)) return hr;

    hr = pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { pAttr->Release(); return hr; }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttr, &ppDevices, &count);
    pAttr->Release();
    if (FAILED(hr)) return hr;

    for (UINT32 i = 0; i < count; ++i)
    {
        WCHAR* szName = nullptr;
        UINT32 len = 0;
        if (SUCCEEDED(ppDevices[i]->GetAllocatedString(
                MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &szName, &len)))
        {
            names.push_back(szName);
            CoTaskMemFree(szName);
        }
        else
        {
            names.push_back(L"Unknown device");
        }
        ppDevices[i]->Release();
    }
    CoTaskMemFree(ppDevices);
    return S_OK;
}

HRESULT VideoCapture::Initialize(int deviceIndex, FrameCallback cb)
{
    Release();
    m_callback = cb;

    // Re-enumerate to get the IMFActivate for the chosen device.
    IMFAttributes* pAttr = nullptr;
    HRESULT hr = MFCreateAttributes(&pAttr, 1);
    if (FAILED(hr)) return hr;

    hr = pAttr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { pAttr->Release(); return hr; }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttr, &ppDevices, &count);
    pAttr->Release();
    if (FAILED(hr)) return hr;

    if (deviceIndex < 0 || static_cast<UINT32>(deviceIndex) >= count)
    {
        for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
        return E_INVALIDARG;
    }

    IMFMediaSource* pSource = nullptr;
    hr = ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&pSource));

    for (UINT32 i = 0; i < count; ++i) ppDevices[i]->Release();
    CoTaskMemFree(ppDevices);

    if (FAILED(hr)) return hr;

    // Create source reader attributes — allow format conversion.
    IMFAttributes* pReaderAttr = nullptr;
    hr = MFCreateAttributes(&pReaderAttr, 2);
    if (SUCCEEDED(hr))
    {
        pReaderAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    hr = MFCreateSourceReaderFromMediaSource(pSource, pReaderAttr, &m_pReader);
    if (pReaderAttr) pReaderAttr->Release();
    pSource->Release();
    if (FAILED(hr)) return hr;

    // Request RGB24 output so the encoder receives BGR24 (MF flips it).
    IMFMediaType* pType = nullptr;
    hr = MFCreateMediaType(&pType);
    if (FAILED(hr)) return hr;

    pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    pType->SetGUID(MF_MT_SUBTYPE,    MFVideoFormat_RGB24);
    hr = m_pReader->SetCurrentMediaType(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);
    pType->Release();

    return hr;
}

void VideoCapture::Start()
{
    if (m_running.exchange(true)) return;
    m_thread = std::thread(&VideoCapture::CaptureLoop, this);
}

void VideoCapture::Stop()
{
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void VideoCapture::Release()
{
    if (m_pReader) { m_pReader->Release(); m_pReader = nullptr; }
}

void VideoCapture::CaptureLoop()
{
    while (m_running)
    {
        DWORD   streamIndex = 0, flags = 0;
        LONGLONG timestamp  = 0;
        IMFSample* pSample  = nullptr;

        HRESULT hr = m_pReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0, &streamIndex, &flags, &timestamp, &pSample);

        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) break;
        if (!pSample) continue;

        IMFMediaBuffer* pBuffer = nullptr;
        if (SUCCEEDED(pSample->ConvertToContiguousBuffer(&pBuffer)))
        {
            BYTE*  pData   = nullptr;
            DWORD  maxLen  = 0, curLen = 0;

            if (SUCCEEDED(pBuffer->Lock(&pData, &maxLen, &curLen)))
            {
                // Retrieve frame dimensions from current media type.
                IMFMediaType* pCurrent = nullptr;
                UINT32 w = 0, h = 0;
                if (SUCCEEDED(m_pReader->GetCurrentMediaType(
                        MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrent)))
                {
                    MFGetAttributeSize(pCurrent, MF_MT_FRAME_SIZE, &w, &h);
                    pCurrent->Release();
                }

                if (w > 0 && h > 0 && m_callback)
                {
                    int stride = static_cast<int>(w) * 3;
                    // MF RGB24 is stored bottom-up; flip to top-down for encoder.
                    std::vector<uint8_t> flipped(curLen);
                    for (UINT32 row = 0; row < h; ++row)
                    {
                        const uint8_t* src = pData + (h - 1 - row) * stride;
                        uint8_t*       dst = flipped.data() + row * stride;
                        memcpy(dst, src, stride);
                    }
                    // MF RGB24 byte order is B,G,R  -> isBGR = true
                    m_callback(flipped.data(),
                               static_cast<int>(w),
                               static_cast<int>(h),
                               stride);
                }
                pBuffer->Unlock();
            }
            pBuffer->Release();
        }
        pSample->Release();
    }
}
