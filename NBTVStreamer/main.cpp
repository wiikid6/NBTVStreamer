#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "MainWindow.h"

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE /*hPrevInstance*/,
                   LPSTR     /*lpCmdLine*/,
                   int        nCmdShow)
{
    return MainWindow::Run(hInstance, nCmdShow);
}
