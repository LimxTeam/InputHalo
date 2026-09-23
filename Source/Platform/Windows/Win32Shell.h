#pragma once
#include <Windows.h>
#include <shellapi.h>
#include <expected>
#include <string>
#include <utility>
namespace ih::platform {
std::expected<void,std::string> SetRunAtStartup(bool enabled);
class Win32Tray {
public:
 ~Win32Tray();
 bool Initialize(HWND settings);
 // 1=open, 2=toggle overlays, 3=exit. Menu actions only; no input data.
 int TakeAction(){return std::exchange(m_action,0);}
 void Enabled(bool value){m_enabled=value;}
 void Chinese(bool value){m_chinese=value;}
private:
 static LRESULT CALLBACK Proc(HWND,UINT,WPARAM,LPARAM);
 void Add();
 HWND m_window{},m_settings{};NOTIFYICONDATAW m_icon{};int m_action{};bool m_enabled=true;UINT m_restart{};
 bool m_chinese=true;
};
}
