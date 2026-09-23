/*******************************************************************************
 * 文件: Win32Shell.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现开机自启注册表操作与系统托盘图标：注册/取消开机启动，创建托盘图标与右键菜单（打开/暂停/退出）。
 *   不涉及复杂算法；注册表与 Shell 通知 API 调用。
 *   空间-时间权衡：托盘用隐藏工具窗口接收消息，无额外缓冲。
 *
 * 设计哲学:
 *   通过注册表 Run 键实现开机自启；通过 Shell_NotifyIcon 管理托盘图标，托盘菜单动作以整数编码供主循环消费。
 *
 * 技术特性:
 *   - 开机自启：HKCU\...\Run 注册表写入/删除
 *   - 托盘图标：NOTIFYICONDATAW 注册图标、提示文本、回调消息
 *   - 任务栏重启响应：监听 TaskbarCreated 注册消息，资源管理器重启后重建托盘
 *   - 右键菜单：弹出式菜单（打开/暂停提示/退出），中英双语
 *
 * 依赖关系:
 *   外部依赖：Win32 注册表 API、Shell32（Shell_NotifyIcon）、Resources.h（IDI_INPUTHALO）
 *
 * 注意事项:
 *   托盘菜单动作以 m_action 整数传递（1=打开 2=切换 3=退出），不直接执行逻辑
 *   删除不存在的注册表值视为成功（幂等）
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Shell.h"
#include "Platform/Windows/Win32Paths.h"
#include "Platform/Windows/Resources.h"
// Win32 Shell 集成命名空间
namespace ih::platform {
// 设置开机自启
// enabled=true 写入注册表 Run 键；false 删除；返回错误详情
std::expected<void,std::string> SetRunAtStartup(bool enabled){
    // 打开 HKCU 启动项注册表键
 HKEY key{};auto result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
 if(result!=ERROR_SUCCESS)return std::unexpected("Cannot open startup preferences: "+std::to_string(result));
    // 启用：获取 exe 路径并写入带引号的命令行
 if(enabled){wchar_t path[32768]{};DWORD length=GetModuleFileNameW(nullptr,path,32768);if(length==0||length>=32768){RegCloseKey(key);return std::unexpected("Cannot resolve application path");}std::wstring command=L"\""+std::wstring(path)+L"\"";result=RegSetValueExW(key,L"InputHalo",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));}
    // 禁用：删除启动项（不存在视为成功）
 else{result=RegDeleteValueW(key,L"InputHalo");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}
    // 关闭注册表键并返回结果
 RegCloseKey(key);if(result!=ERROR_SUCCESS)return std::unexpected("Cannot update startup preferences: "+std::to_string(result));return {};
}
// 托盘析构：删除托盘图标、销毁窗口、注销窗口类
Win32Tray::~Win32Tray(){if(m_window){Shell_NotifyIconW(NIM_DELETE,&m_icon);DestroyWindow(m_window);UnregisterClassW(L"InputHalo.Tray",GetModuleHandleW(nullptr));}}
// 重新添加托盘图标（用于任务栏重建后）
void Win32Tray::Add(){Shell_NotifyIconW(NIM_ADD,&m_icon);}
// 初始化托盘：注册窗口类、创建隐藏窗口、配置托盘图标
// settings 为设置窗口句柄（托盘消息关联）
bool Win32Tray::Initialize(HWND settings){m_settings=settings;WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"InputHalo.Tray";if(!RegisterClassW(&wc))return false;m_window=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,this);if(!m_window)return false;m_restart=RegisterWindowMessageW(L"TaskbarCreated");m_icon.cbSize=sizeof(m_icon);m_icon.hWnd=m_window;m_icon.uID=1;m_icon.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;m_icon.uCallbackMessage=WM_APP+1;m_icon.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_INPUTHALO));wcscpy_s(m_icon.szTip,L"InputHalo - input visualization");return Shell_NotifyIconW(NIM_ADD,&m_icon)!=FALSE;}
// 托盘窗口过程：处理托盘图标消息与右键菜单
LRESULT CALLBACK Win32Tray::Proc(HWND window,UINT message,WPARAM w,LPARAM l){auto* self=reinterpret_cast<Win32Tray*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Win32Tray*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(self){if(self->m_restart&&message==self->m_restart){self->Add();return 0;}if(message==WM_APP+1){if(l==WM_LBUTTONUP||l==WM_LBUTTONDBLCLK)self->m_action=1;if(l==WM_RBUTTONUP){auto menu=CreatePopupMenu();if(menu){AppendMenuW(menu,MF_STRING,1,self->m_chinese?L"打开 InputHalo":L"Open InputHalo");AppendMenuW(menu,MF_STRING,2,self->m_chinese?(self->m_enabled?L"暂停提示":L"开启提示"):(self->m_enabled?L"Pause overlays":L"Enable overlays"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,3,self->m_chinese?L"退出":L"Exit");POINT pt{};GetCursorPos(&pt);SetForegroundWindow(window);self->m_action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,window,nullptr);DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);}}return 0;}}return DefWindowProcW(window,message,w,l);}
}

