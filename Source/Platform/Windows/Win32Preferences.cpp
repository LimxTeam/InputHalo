#include "Platform/Windows/Win32Preferences.h"
#include <commdlg.h>
#include <array>
#include <cwchar>
namespace ih::platform {
std::expected<std::optional<std::filesystem::path>,std::string> PickPreferenceFile(HWND owner,bool save,bool executable,bool chinese,const std::filesystem::path& initial){
    std::array<wchar_t,32768> filename{};
    if(save)wcscpy_s(filename.data(),filename.size(),L"InputHalo preset.json");
    OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=owner;dialog.lpstrFile=filename.data();dialog.nMaxFile=static_cast<DWORD>(filename.size());
    dialog.lpstrFilter=executable?L"Windows application (*.exe)\0*.exe\0\0":L"InputHalo preset (*.json)\0*.json\0\0";
    dialog.lpstrDefExt=executable?L"exe":L"json";
    dialog.lpstrTitle=chinese?(executable?L"选择要排除的程序":save?L"保存 InputHalo 方案（文件名即方案名）":L"导入 InputHalo 方案"):(executable?L"Choose an application to exclude":save?L"Save InputHalo preset (filename is the preset name)":L"Import InputHalo preset");
    dialog.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_DONTADDTORECENT|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    const BOOL accepted=save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);
    if(accepted)return std::optional<std::filesystem::path>(filename.data());
    if(CommDlgExtendedError())return std::unexpected("Could not open the file picker");
    return std::optional<std::filesystem::path>{};
}
bool ForegroundExcluded(const SettingsModel& settings){
    const auto& apps=settings.values["exclusions"]["apps"];if(apps.empty())return false;
    static HWND previous{};static DWORD previousPid{};static std::wstring name;static ULONGLONG refreshed{};
    const HWND window=GetForegroundWindow();DWORD pid{};GetWindowThreadProcessId(window,&pid);
    const auto tick=GetTickCount64();
    if(window!=previous||pid!=previousPid||tick-refreshed>1000){
        previous=window;previousPid=pid;refreshed=tick;name.clear();
        if(HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)){
            wchar_t path[32768]{};DWORD length=32768;
            if(QueryFullProcessImageNameW(process,0,path,&length))name=std::filesystem::path(path).filename().wstring();
            CloseHandle(process);
        }
    }
    if(name.empty())return false;
    for(const auto& app:apps){
        const auto& utf8=app.get_ref<const std::string&>();
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,nullptr,0);if(length<=1)continue;
        std::wstring wide(static_cast<size_t>(length),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,wide.data(),length);
        if(CompareStringOrdinal(name.c_str(),-1,wide.c_str(),-1,TRUE)==CSTR_EQUAL)return true;
    }
    return false;
}
}
