#include "MainWindow.h"
#include "resource.h"
#include <mfapi.h>
#include <commctrl.h>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------------------
// Static entry point
// ---------------------------------------------------------------------------
int MainWindow::Run(HINSTANCE hInst, int /*nCmdShow*/)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);

    MainWindow wnd(hInst);

    INT_PTR result = DialogBoxParamW(hInst,
                                     MAKEINTRESOURCEW(IDD_MAIN_DIALOG),
                                     nullptr,
                                     &MainWindow::DlgProc,
                                     reinterpret_cast<LPARAM>(&wnd));

    MFShutdown();
    CoUninitialize();
    return static_cast<int>(result);
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
MainWindow::MainWindow(HINSTANCE hInst) : m_hInst(hInst) {}

MainWindow::~MainWindow()
{
    OnStop();
}

// ---------------------------------------------------------------------------
// Dialog procedure (static trampoline)
// ---------------------------------------------------------------------------
INT_PTR CALLBACK MainWindow::DlgProc(HWND hDlg, UINT msg,
                                      WPARAM wParam, LPARAM lParam)
{
    MainWindow* pThis = nullptr;
    if (msg == WM_INITDIALOG)
    {
        pThis = reinterpret_cast<MainWindow*>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<MainWindow*>(
                    GetWindowLongPtrW(hDlg, DWLP_USER));
    }

    if (pThis) return pThis->HandleMessage(hDlg, msg, wParam, lParam);
    return FALSE;
}

INT_PTR MainWindow::HandleMessage(HWND hDlg, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
        m_hDlg = hDlg;
        OnInitDialog(hDlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_BTN_STREAM: OnStream();            return TRUE;
        case IDC_BTN_STOP:   OnStop();              return TRUE;
        case IDCANCEL:
            OnStop();
            EndDialog(hDlg, 0);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        OnStop();
        EndDialog(hDlg, 0);
        return TRUE;
    }
    return FALSE;
}

// ---------------------------------------------------------------------------
// WM_INITDIALOG
// ---------------------------------------------------------------------------
void MainWindow::OnInitDialog(HWND hDlg)
{
    // Disable Stop button initially.
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_STOP), FALSE);

    // Populate video device list.
    std::vector<std::wstring> vidNames;
    VideoCapture::EnumerateDevices(vidNames);

    HWND hVidList = GetDlgItem(hDlg, IDC_LIST_VIDEO);
    for (auto& name : vidNames)
        SendMessageW(hVidList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name.c_str()));
    if (!vidNames.empty())
        SendMessageW(hVidList, LB_SETCURSEL, 0, 0);

    // Populate audio output list.
    std::vector<std::wstring> audNames;
    AudioOutput::EnumerateDevices(audNames);

    HWND hAudList = GetDlgItem(hDlg, IDC_LIST_AUDIO);
    for (auto& name : audNames)
        SendMessageW(hAudList, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(name.c_str()));
    if (!audNames.empty())
        SendMessageW(hAudList, LB_SETCURSEL, 0, 0);

    if (vidNames.empty())
        SetStatus(L"Warning: no video capture devices found.");
    else if (audNames.empty())
        SetStatus(L"Warning: no audio output devices found.");
}

// ---------------------------------------------------------------------------
// Stream button
// ---------------------------------------------------------------------------
void MainWindow::OnStream()
{
    if (m_streaming) return;

    // Read selections.
    HWND hVidList = GetDlgItem(m_hDlg, IDC_LIST_VIDEO);
    HWND hAudList = GetDlgItem(m_hDlg, IDC_LIST_AUDIO);

    int vidIdx = static_cast<int>(
        SendMessageW(hVidList, LB_GETCURSEL, 0, 0));
    int audIdx = static_cast<int>(
        SendMessageW(hAudList, LB_GETCURSEL, 0, 0));

    if (vidIdx == LB_ERR || audIdx == LB_ERR)
    {
        SetStatus(L"Please select a video device and an audio output device.");
        return;
    }

    // Initialize audio output first so we know its sample rate.
    m_queue   = std::make_shared<AudioQueue>();
    m_audio   = std::make_unique<AudioOutput>();

    SetStatus(L"Initialising audio output...");
    HRESULT hr = m_audio->Initialize(audIdx, m_queue);
    if (FAILED(hr))
    {
        SetStatus(L"Failed to initialise audio output.");
        m_audio.reset();
        return;
    }

    int sampleRate = m_audio->GetSampleRate();

    // Create encoder with the device's actual sample rate.
    m_encoder = std::make_unique<NBTVEncoder>(sampleRate);

    // Capture callback: encode each frame and push audio to queue.
    auto* enc   = m_encoder.get();
    auto  queue = m_queue;

    FrameCallback cb = [enc, queue](const uint8_t* px,
                                    int w, int h, int stride)
    {
        std::vector<float> samples;
        enc->EncodeFrame(px, w, h, stride, /*isBGR=*/true, samples);
        queue->Push(samples);
    };

    m_video = std::make_unique<VideoCapture>();

    SetStatus(L"Initialising video capture...");
    hr = m_video->Initialize(vidIdx, cb);
    if (FAILED(hr))
    {
        SetStatus(L"Failed to initialise video capture.");
        m_video.reset();
        m_audio.reset();
        m_encoder.reset();
        return;
    }

    // Start both threads.
    m_audio->Start();
    m_video->Start();

    m_streaming = true;
    EnableWindow(GetDlgItem(m_hDlg, IDC_BTN_STREAM), FALSE);
    EnableWindow(GetDlgItem(m_hDlg, IDC_BTN_STOP),   TRUE);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LIST_VIDEO),  FALSE);
    EnableWindow(GetDlgItem(m_hDlg, IDC_LIST_AUDIO),  FALSE);
    SetStatus(L"Streaming NBTV signal. Press Stop to halt.");
}

// ---------------------------------------------------------------------------
// Stop button
// ---------------------------------------------------------------------------
void MainWindow::OnStop()
{
    if (!m_streaming) return;

    SetStatus(L"Stopping...");

    if (m_video)   m_video->Stop();
    if (m_queue)   m_queue->Close();
    if (m_audio)   m_audio->Stop();

    m_video.reset();
    m_audio.reset();
    m_encoder.reset();
    m_queue.reset();

    m_streaming = false;

    if (m_hDlg)
    {
        EnableWindow(GetDlgItem(m_hDlg, IDC_BTN_STREAM), TRUE);
        EnableWindow(GetDlgItem(m_hDlg, IDC_BTN_STOP),   FALSE);
        EnableWindow(GetDlgItem(m_hDlg, IDC_LIST_VIDEO),  TRUE);
        EnableWindow(GetDlgItem(m_hDlg, IDC_LIST_AUDIO),  TRUE);
        SetStatus(L"Stopped. Ready.");
    }
}

// ---------------------------------------------------------------------------
// Status text helper
// ---------------------------------------------------------------------------
void MainWindow::SetStatus(const wchar_t* text)
{
    if (m_hDlg)
        SetDlgItemTextW(m_hDlg, IDC_STATIC_STATUS, text);
}
