#include "Platform/Windows/Win32Input.h"
#include "Core/Clock.h"
#include <WtsApi32.h>
#include <utility>
#include <stdexcept>
namespace ih::platform {
input::Key MapKey(uint16_t vk, uint16_t scan, uint16_t flags) {
    using input::Key;
    const bool extended = (flags & RI_KEY_E0) != 0;
    if (vk == 255 || scan == 0xff) return Key::Unknown;
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(input::Index(Key::A) + vk - 'A');
    if (vk >= '0' && vk <= '9') return static_cast<Key>(input::Index(Key::Num0) + vk - '0');
    if (vk >= VK_F1 && vk <= VK_F24) return static_cast<Key>(input::Index(Key::F1) + vk - VK_F1);
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return static_cast<Key>(input::Index(Key::Numpad0) + vk - VK_NUMPAD0);
    switch (vk) {
    case VK_CONTROL:return extended ? Key::RightCtrl : Key::LeftCtrl;
    case VK_LCONTROL:return Key::LeftCtrl; case VK_RCONTROL:return Key::RightCtrl;
    case VK_SHIFT:return scan == 0x36 ? Key::RightShift : Key::LeftShift;
    case VK_LSHIFT:return Key::LeftShift; case VK_RSHIFT:return Key::RightShift;
    case VK_MENU:return extended ? Key::RightAlt : Key::LeftAlt;
    case VK_LMENU:return Key::LeftAlt; case VK_RMENU:return Key::RightAlt;
    case VK_LWIN:return Key::LeftWin; case VK_RWIN:return Key::RightWin;
    case VK_ESCAPE:return Key::Escape; case VK_TAB:return Key::Tab;
    case VK_RETURN:return extended ? Key::NumpadEnter : Key::Enter;
    case VK_SPACE:return Key::Space; case VK_BACK:return Key::Backspace;
    case VK_DELETE:return extended ? Key::Delete : Key::Decimal;
    case VK_INSERT:return extended ? Key::Insert : Key::Numpad0;
    case VK_END:return extended ? Key::End : Key::Numpad1;
    case VK_DOWN:return extended ? Key::Down : Key::Numpad2;
    case VK_NEXT:return extended ? Key::PageDown : Key::Numpad3;
    case VK_LEFT:return extended ? Key::Left : Key::Numpad4;
    case VK_CLEAR:return Key::Numpad5;
    case VK_RIGHT:return extended ? Key::Right : Key::Numpad6;
    case VK_HOME:return extended ? Key::Home : Key::Numpad7;
    case VK_UP:return extended ? Key::Up : Key::Numpad8;
    case VK_PRIOR:return extended ? Key::PageUp : Key::Numpad9;
    case VK_CAPITAL:return Key::CapsLock; case VK_NUMLOCK:return Key::NumLock; case VK_SCROLL:return Key::ScrollLock;
    case VK_SNAPSHOT:return Key::PrintScreen; case VK_PAUSE:return Key::Pause; case VK_APPS:return Key::Menu;
    case VK_ADD:return Key::Add; case VK_SUBTRACT:return Key::Subtract; case VK_MULTIPLY:return Key::Multiply;
    case VK_DIVIDE:return Key::Divide; case VK_DECIMAL:return Key::Decimal;
    case VK_OEM_1:return Key::Semicolon; case VK_OEM_7:return Key::Apostrophe; case VK_OEM_3:return Key::Grave;
    case VK_OEM_MINUS:return Key::Minus; case VK_OEM_PLUS:return Key::Equals; case VK_OEM_4:return Key::LeftBracket;
    case VK_OEM_6:return Key::RightBracket; case VK_OEM_5:case VK_OEM_102:return Key::Backslash;
    case VK_OEM_COMMA:return Key::Comma; case VK_OEM_PERIOD:return Key::Period; case VK_OEM_2:return Key::Slash;
    default:return Key::Unknown;
    }
}
Win32Input::~Win32Input() {
    if (m_window) {
        const RAWINPUTDEVICE devices[]{{1,6,RIDEV_REMOVE,nullptr},{1,2,RIDEV_REMOVE,nullptr}};
        RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
        WTSUnRegisterSessionNotification(m_window);
        DestroyWindow(m_window);
        UnregisterClassW(L"InputHalo.RawInput", GetModuleHandleW(nullptr));
    }
}
std::expected<void, std::string> Win32Input::Initialize() {
    WNDCLASSW definition{}; definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpfnWndProc = Proc; definition.lpszClassName = L"InputHalo.RawInput";
    if (!RegisterClassW(&definition)) return std::unexpected("Cannot register input receiver class");
    m_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, definition.lpszClassName, L"", WS_POPUP,
        0,0,0,0,nullptr,nullptr,definition.hInstance,this);
    if (!m_window) { UnregisterClassW(definition.lpszClassName, definition.hInstance); return std::unexpected("Cannot create input receiver"); }
    const RAWINPUTDEVICE devices[]{{1,6,RIDEV_INPUTSINK | RIDEV_DEVNOTIFY,m_window}, {1,2,RIDEV_INPUTSINK | RIDEV_DEVNOTIFY,m_window}};
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) return std::unexpected("Cannot register Raw Input");
    WTSRegisterSessionNotification(m_window, NOTIFY_FOR_THIS_SESSION);
    return {};
}
std::optional<render2d::Point> Win32Input::TryCursorPosition() {
    POINT point{};if(!GetCursorPos(&point))return std::nullopt;
    return render2d::Point{static_cast<float>(point.x),static_cast<float>(point.y)};
}
render2d::Point Win32Input::CursorPosition() { return TryCursorPosition().value_or(render2d::Point{}); }
bool Win32Input::DesktopAvailable() {
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return false;
    wchar_t name[128]{}; DWORD bytes{};
    const bool available = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes) && wcscmp(name, L"Default") == 0;
    CloseDesktop(desktop); return available;
}
bool Win32Input::ReduceMotion() { BOOL enabled = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0); return !enabled; }
void Win32Input::Read(HRAWINPUT handle) {
    RAWINPUT input{}; UINT bytes = sizeof(input);
    if (GetRawInputData(handle, RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return;
    const auto cursor=TryCursorPosition();
    if(!cursor){m_events.clear();m_events.push_back({input::EventType::Reset});return;}
    const auto time = Now(); const auto position = *cursor;
    if (m_events.size() > 4096) { m_events.clear(); m_events.push_back({input::EventType::Reset}); }
    if (input.header.dwType == RIM_TYPEKEYBOARD) {
        const auto& key = input.data.keyboard;
        const auto mapped = MapKey(key.VKey, key.MakeCode, key.Flags);
        if (mapped != input::Key::Unknown) {
            m_events.push_back({(key.Flags & RI_KEY_BREAK) ? input::EventType::Up : input::EventType::Down, mapped, 0, time, position});
            if (mapped == input::Key::Pause) m_events.push_back({input::EventType::Up, mapped, 0, time, position});
        }
    } else if (input.header.dwType == RIM_TYPEMOUSE) {
        const auto flags = input.data.mouse.usButtonFlags;
        constexpr input::Key buttons[]{input::Key::MouseLeft,input::Key::MouseRight,input::Key::MouseMiddle,input::Key::Mouse4,input::Key::Mouse5};
        for (unsigned i = 0; i < 5; ++i) {
            if (flags & (1u << (i * 2))) m_events.push_back({input::EventType::Down,buttons[i],0,time,position});
            if (flags & (2u << (i * 2))) m_events.push_back({input::EventType::Up,buttons[i],0,time,position});
        }
        if (flags & RI_MOUSE_WHEEL) m_events.push_back({input::EventType::Wheel,{},static_cast<SHORT>(input.data.mouse.usButtonData) / 120.f,time,position});
    }
}
LRESULT CALLBACK Win32Input::Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Win32Input*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<Win32Input*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (self) try {
        if (message == WM_INPUT) self->Read(reinterpret_cast<HRAWINPUT>(lParam));
        if ((message == WM_INPUT_DEVICE_CHANGE && wParam == GIDC_REMOVAL) || message == WM_WTSSESSION_CHANGE ||
            (message == WM_POWERBROADCAST && wParam == PBT_APMSUSPEND)) {
            self->m_events.clear(); self->m_events.push_back({input::EventType::Reset});
        }
    } catch (...) { self->m_events.clear(); self->m_failed = true; }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
