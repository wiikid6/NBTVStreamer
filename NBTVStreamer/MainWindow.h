#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <memory>
#include "VideoCapture.h"
#include "AudioOutput.h"
#include "NBTVEncoder.h"
#include "AudioQueue.h"

class MainWindow
{
public:
    // Entry point: creates the dialog and runs the message loop.
    static int Run(HINSTANCE hInst, int nCmdShow);

private:
    explicit MainWindow(HINSTANCE hInst);
    ~MainWindow();

    // Win32 dialog procedure (static trampoline).
    static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg,
                                     WPARAM wParam, LPARAM lParam);
    INT_PTR HandleMessage(HWND hDlg, UINT msg,
                           WPARAM wParam, LPARAM lParam);

    void OnInitDialog(HWND hDlg);
    void OnStream();
    void OnStop();
    void SetStatus(const wchar_t* text);

    HINSTANCE   m_hInst = nullptr;
    HWND        m_hDlg  = nullptr;

    std::unique_ptr<VideoCapture>       m_video;
    std::unique_ptr<AudioOutput>        m_audio;
    std::unique_ptr<NBTVEncoder>        m_encoder;
    std::shared_ptr<AudioQueue>         m_queue;

    bool        m_streaming = false;
};
