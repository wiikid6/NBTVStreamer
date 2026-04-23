#include "AudioOutput.h"
#include <cstring>
#include <algorithm>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

static constexpr REFERENCE_TIME kBufferDuration = 200000; // 20 ms in 100-ns units

AudioOutput::AudioOutput() = default;

AudioOutput::~AudioOutput()
{
    Stop();
    Release();
}

HRESULT AudioOutput::EnumerateDevices(std::vector<std::wstring>& names)
{
    names.clear();

    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&pEnum));
    if (FAILED(hr)) return hr;

    IMMDeviceCollection* pColl = nullptr;
    hr = pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pColl);
    pEnum->Release();
    if (FAILED(hr)) return hr;

    UINT count = 0;
    pColl->GetCount(&count);

    for (UINT i = 0; i < count; ++i)
    {
        IMMDevice* pDev = nullptr;
        if (FAILED(pColl->Item(i, &pDev))) continue;

        IPropertyStore* pProps = nullptr;
        if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pProps)))
        {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &name))
                && name.vt == VT_LPWSTR)
            {
                names.push_back(name.pwszVal);
            }
            else
            {
                names.push_back(L"Unknown device");
            }
            PropVariantClear(&name);
            pProps->Release();
        }
        pDev->Release();
    }
    pColl->Release();
    return S_OK;
}

HRESULT AudioOutput::Initialize(int deviceIndex, std::shared_ptr<AudioQueue> queue)
{
    Release();
    m_queue = queue;

    IMMDeviceEnumerator* pEnum = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&pEnum));
    if (FAILED(hr)) return hr;

    IMMDeviceCollection* pColl = nullptr;
    hr = pEnum->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &pColl);
    pEnum->Release();
    if (FAILED(hr)) return hr;

    UINT count = 0;
    pColl->GetCount(&count);
    if (deviceIndex < 0 || static_cast<UINT>(deviceIndex) >= count)
    {
        pColl->Release();
        return E_INVALIDARG;
    }

    hr = pColl->Item(static_cast<UINT>(deviceIndex), &m_pDevice);
    pColl->Release();
    if (FAILED(hr)) return hr;

    hr = m_pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL,
                              nullptr, reinterpret_cast<void**>(&m_pAudioClient));
    if (FAILED(hr)) return hr;

    hr = m_pAudioClient->GetMixFormat(&m_pFormat);
    if (FAILED(hr)) return hr;

    m_sampleRate = static_cast<int>(m_pFormat->nSamplesPerSec);
    m_channels   = static_cast<int>(m_pFormat->nChannels);

    // Determine if the mix format is float.
    if (m_pFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
    {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(m_pFormat);
        m_isFloat = (ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    }
    else
    {
        m_isFloat = (m_pFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    }

    m_hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_hEvent) return HRESULT_FROM_WIN32(GetLastError());

    hr = m_pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            kBufferDuration, 0, m_pFormat, nullptr);
    if (FAILED(hr)) return hr;

    hr = m_pAudioClient->SetEventHandle(m_hEvent);
    if (FAILED(hr)) return hr;

    hr = m_pAudioClient->GetService(IID_PPV_ARGS(&m_pRenderClient));
    return hr;
}

void AudioOutput::Start()
{
    if (m_running.exchange(true)) return;
    m_pAudioClient->Start();
    m_thread = std::thread(&AudioOutput::RenderLoop, this);
}

void AudioOutput::Stop()
{
    m_running = false;
    if (m_hEvent) SetEvent(m_hEvent);   // unblock the wait
    if (m_thread.joinable()) m_thread.join();
    if (m_pAudioClient) m_pAudioClient->Stop();
}

void AudioOutput::Release()
{
    if (m_pFormat)       { CoTaskMemFree(m_pFormat); m_pFormat = nullptr; }
    if (m_pRenderClient) { m_pRenderClient->Release(); m_pRenderClient = nullptr; }
    if (m_pAudioClient)  { m_pAudioClient->Release();  m_pAudioClient = nullptr;  }
    if (m_pDevice)       { m_pDevice->Release();       m_pDevice = nullptr;       }
    if (m_hEvent)        { CloseHandle(m_hEvent);      m_hEvent = nullptr;        }
}

void AudioOutput::RenderLoop()
{
    UINT32 bufferFrames = 0;
    m_pAudioClient->GetBufferSize(&bufferFrames);

    // Temporary mono float buffer.
    std::vector<float> monoSrc(bufferFrames);

    while (m_running)
    {
        DWORD waitResult = WaitForSingleObject(m_hEvent, 200);
        if (waitResult != WAIT_OBJECT_0) continue;
        if (!m_running) break;

        UINT32 padding = 0;
        if (FAILED(m_pAudioClient->GetCurrentPadding(&padding))) break;

        UINT32 framesAvail = bufferFrames - padding;
        if (framesAvail == 0) continue;

        // Pull mono samples from queue (blocks briefly if empty).
        bool ok = m_queue->Pop(monoSrc.data(), framesAvail);
        if (!ok) break;

        BYTE* pData = nullptr;
        if (FAILED(m_pRenderClient->GetBuffer(framesAvail, &pData))) break;

        // Write to device buffer, duplicating mono to all channels.
        if (m_isFloat)
        {
            float* dst = reinterpret_cast<float*>(pData);
            for (UINT32 f = 0; f < framesAvail; ++f)
                for (int ch = 0; ch < m_channels; ++ch)
                    dst[f * m_channels + ch] = monoSrc[f];
        }
        else
        {
            // 16-bit PCM fallback.
            int16_t* dst = reinterpret_cast<int16_t*>(pData);
            for (UINT32 f = 0; f < framesAvail; ++f)
            {
                int16_t s = static_cast<int16_t>(
                    std::clamp(monoSrc[f], 0.0f, 1.0f) * 32767.0f);
                for (int ch = 0; ch < m_channels; ++ch)
                    dst[f * m_channels + ch] = s;
            }
        }

        m_pRenderClient->ReleaseBuffer(framesAvail, 0);
    }
}
