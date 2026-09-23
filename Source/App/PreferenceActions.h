#pragma once
#include "UI/LibraryPanel.h"
#include <Windows.h>
namespace ih {
bool HandlePreferenceAction(HWND owner,const std::filesystem::path& directory,ui::LibraryPanel& panel,ui::LibraryAction action,SettingsModel& settings);
}
