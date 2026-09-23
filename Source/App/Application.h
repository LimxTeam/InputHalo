#pragma once
#include "Platform/Windows/Win32Window.h"
#include "Settings/SettingsModel.h"

namespace ih {
class Application {
public:
    int Run(HINSTANCE instance, int showCommand);
private:
    SettingsModel m_settings = SettingsModel::Defaults();
    platform::Win32Window m_window;
};
}
