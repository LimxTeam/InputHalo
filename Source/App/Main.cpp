#include "App/Application.h"
#include "Platform/Windows/Win32Dpi.h"
#include <exception>
#include <ShObjIdl.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    try {
        SetCurrentProcessExplicitAppUserModelID(L"LimxTeam.InputHalo");
        if (auto dpi = ih::platform::InitializeDpi(); !dpi) {
            OutputDebugStringA(dpi.error().c_str());
            return 1;
        }
        ih::Application application;
        return application.Run(instance, showCommand);
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        MessageBoxA(nullptr,error.what(),"InputHalo could not continue",MB_OK|MB_ICONERROR);
        return 1;
    }
}
