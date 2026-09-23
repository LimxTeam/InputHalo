#include "Platform/Windows/Win32Shell.h"
#include "Platform/Windows/Win32Paths.h"
#include "Platform/Windows/Resources.h"
namespace ih::platform {
std::expected<void,std::string> SetRunAtStartup(bool enabled){
 HKEY key{};auto result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr);
 if(result!=ERROR_SUCCESS)return std::unexpected("Cannot open startup preferences: "+std::to_string(result));
 if(enabled){wchar_t path[32768]{};DWORD length=GetModuleFileNameW(nullptr,path,32768);if(length==0||length>=32768){RegCloseKey(key);return std::unexpected("Cannot resolve application path");}std::wstring command=L"\""+std::wstring(path)+L"\"";result=RegSetValueExW(key,L"InputHalo",0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));}
 else{result=RegDeleteValueW(key,L"InputHalo");if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;}
 RegCloseKey(key);if(result!=ERROR_SUCCESS)return std::unexpected("Cannot update startup preferences: "+std::to_string(result));return {};
}
Win32Tray::~Win32Tray(){if(m_window){Shell_NotifyIconW(NIM_DELETE,&m_icon);DestroyWindow(m_window);UnregisterClassW(L"InputHalo.Tray",GetModuleHandleW(nullptr));}}
void Win32Tray::Add(){Shell_NotifyIconW(NIM_ADD,&m_icon);}
bool Win32Tray::Initialize(HWND settings){m_settings=settings;WNDCLASSW wc{};wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"InputHalo.Tray";if(!RegisterClassW(&wc))return false;m_window=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,this);if(!m_window)return false;m_restart=RegisterWindowMessageW(L"TaskbarCreated");m_icon.cbSize=sizeof(m_icon);m_icon.hWnd=m_window;m_icon.uID=1;m_icon.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;m_icon.uCallbackMessage=WM_APP+1;m_icon.hIcon=LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDI_INPUTHALO));wcscpy_s(m_icon.szTip,L"InputHalo - input visualization");return Shell_NotifyIconW(NIM_ADD,&m_icon)!=FALSE;}
LRESULT CALLBACK Win32Tray::Proc(HWND window,UINT message,WPARAM w,LPARAM l){auto* self=reinterpret_cast<Win32Tray*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){self=static_cast<Win32Tray*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(self){if(self->m_restart&&message==self->m_restart){self->Add();return 0;}if(message==WM_APP+1){if(l==WM_LBUTTONUP||l==WM_LBUTTONDBLCLK)self->m_action=1;if(l==WM_RBUTTONUP){auto menu=CreatePopupMenu();if(menu){AppendMenuW(menu,MF_STRING,1,self->m_chinese?L"打开 InputHalo":L"Open InputHalo");AppendMenuW(menu,MF_STRING,2,self->m_chinese?(self->m_enabled?L"暂停提示":L"开启提示"):(self->m_enabled?L"Pause overlays":L"Enable overlays"));AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,3,self->m_chinese?L"退出":L"Exit");POINT pt{};GetCursorPos(&pt);SetForegroundWindow(window);self->m_action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,window,nullptr);DestroyMenu(menu);PostMessageW(window,WM_NULL,0,0);}}return 0;}}return DefWindowProcW(window,message,w,l);}
}

