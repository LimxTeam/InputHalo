/*******************************************************************************
 * 文件: Win32Preferences.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现偏好文件选择对话框与前台进程排除检测：打开/保存 JSON 预设文件，查询前台窗口进程是否在排除列表中。
 *   算法复杂度：PickPreferenceFile 为 O(1) 对话框；ForegroundExcluded 排除匹配 O(n)，n 为排除应用数。
 *   空间-时间权衡：前台进程名缓存 1 秒（static 变量），避免每帧 OpenProcess+QueryFullProcessImageName。
 *
 * 设计哲学:
 *   封装 Win32 通用对话框与注册表/进程查询，为设置界面提供文件选择与排除逻辑。
 *
 * 技术特性:
 *   - 文件选择：GetOpenFileNameW/GetSaveFileNameW，支持中/英标题与 .json/.exe 过滤
 *   - 前台排除：GetForegroundWindow 获取进程路径名，与排除列表（UTF-8）宽字符比较
 *   - 进程缓存：static 变量缓存上次前台进程名，1 秒内复用，降低系统调用开销
 *
 * 依赖关系:
 *   内部依赖：ih::platform、SettingsModel
 *   外部依赖：Win32 通用对话框（commdlg.h）、Process API、Kernel32
 *
 * 注意事项:
 *   排除列表比较使用 CompareStringOrdinal 大小写不敏感
 *   用户取消对话框返回空 optional（非错误）
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Preferences.h"
#include <commdlg.h>
#include <array>
#include <cwchar>
// Win32 偏好设置命名空间
namespace ih::platform {
// 弹出文件选择对话框（保存或打开）
// owner: 父窗口；save: true=保存；executable: true=选 .exe 排除程序；chinese: 中文标题
// 返回选中路径（用户取消为空 optional）；对话框错误返回 unexpected
std::expected<std::optional<std::filesystem::path>,std::string> PickPreferenceFile(HWND owner,bool save,bool executable,bool chinese,const std::filesystem::path& initial){
    // 32K 文件名缓冲
    std::array<wchar_t,32768> filename{};
    // 保存模式预置默认文件名
    if(save)wcscpy_s(filename.data(),filename.size(),L"InputHalo preset.json");
    // 初始化 OPENFILENAMEW 结构
    OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=owner;dialog.lpstrFile=filename.data();dialog.nMaxFile=static_cast<DWORD>(filename.size());
    // 文件过滤器：exe 或 json
    dialog.lpstrFilter=executable?L"Windows application (*.exe)\0*.exe\0\0":L"InputHalo preset (*.json)\0*.json\0\0";
    // 默认扩展名
    dialog.lpstrDefExt=executable?L"exe":L"json";
    // 对话框标题（中/英，按模式切换）
    dialog.lpstrTitle=chinese?(executable?L"选择要排除的程序":save?L"保存 InputHalo 方案（文件名即方案名）":L"导入 InputHalo 方案"):(executable?L"Choose an application to exclude":save?L"Save InputHalo preset (filename is the preset name)":L"Import InputHalo preset");
    dialog.lpstrInitialDir=initial.empty()?nullptr:initial.c_str();
    // 对话框标志：Explorer 风格、不改目录、路径必须存在、不加入最近列表
    dialog.Flags=OFN_EXPLORER|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_DONTADDTORECENT|(save?OFN_OVERWRITEPROMPT:OFN_FILEMUSTEXIST);
    // 按模式调用保存或打开对话框
    const BOOL accepted=save?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);
    // 成功返回文件路径
    if(accepted)return std::optional<std::filesystem::path>(filename.data());
    // CommDlgExtendedError 非零表示对话框错误
    if(CommDlgExtendedError())return std::unexpected("Could not open the file picker");
    return std::optional<std::filesystem::path>{};
}
// 判断前台窗口对应的进程是否在排除列表中
// 排除列表来自设置模型的 exclusions.apps 数组（UTF-8 字符串）
bool ForegroundExcluded(const SettingsModel& settings){
    // 取排除应用列表，为空则直接返回 false
    const auto& apps=settings.values["exclusions"]["apps"];if(apps.empty())return false;
    // static 缓存：上次前台窗口、PID、进程名、刷新时间（1 秒复用）
    static HWND previous{};static DWORD previousPid{};static std::wstring name;static ULONGLONG refreshed{};
    // 获取当前前台窗口与其进程 PID
    const HWND window=GetForegroundWindow();DWORD pid{};GetWindowThreadProcessId(window,&pid);
    const auto tick=GetTickCount64();
    // 前台窗口变化或缓存超过 1 秒则重新查询进程路径
    if(window!=previous||pid!=previousPid||tick-refreshed>1000){
        previous=window;previousPid=pid;refreshed=tick;name.clear();
        // 打开进程（仅查询权限）
        if(HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)){
            wchar_t path[32768]{};DWORD length=32768;
            // 获取进程完整路径并取文件名
            if(QueryFullProcessImageNameW(process,0,path,&length))name=std::filesystem::path(path).filename().wstring();
            // 关闭进程句柄
            CloseHandle(process);
        }
    }
    // 进程名未知则不排除
    if(name.empty())return false;
    // 遍历排除列表，将 UTF-8 转宽字符后与前台进程名比较
    for(const auto& app:apps){
        const auto& utf8=app.get_ref<const std::string&>();
        // UTF-8 转宽字符（先算长度再转换）
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,nullptr,0);if(length<=1)continue;
        std::wstring wide(static_cast<size_t>(length),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8.c_str(),-1,wide.data(),length);
        // 序数比较（大小写不敏感），匹配则排除
        if(CompareStringOrdinal(name.c_str(),-1,wide.c_str(),-1,TRUE)==CSTR_EQUAL)return true;
    }
    return false;
}
}
