/*******************************************************************************
 * 文件: Win32Input.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现 Raw Input 输入采集：注册隐藏窗口接收全局键盘/鼠标原始输入，映射虚拟键码到逻辑 Key 枚举，输出输入事件队列。
 *   算法复杂度：MapKey 为 O(1) 区间判断+switch；每事件处理 O(1)。
 *   空间-时间权衡：事件队列上限 4096 条，超限则重置防内存膨胀；用 Raw Input Sink 模式无需前台焦点。
 *
 * 设计哲学:
 *   通过 RIDEV_INPUTSINK 在后台窗口接收全局输入，屏蔽硬件差异映射到统一 Key 枚举；监听设备变更/锁屏/休眠以重置状态。
 *
 * 技术特性:
 *   - Raw Input：注册键盘(1,6)与鼠标(1,2)设备，全局捕获输入
 *   - 虚拟键映射：将 Win32 VK_* 与扫描码转为逻辑 Key，区分左右修饰键与扩展键
 *   - 小键盘区分：通过 extended 标志区分方向键与小键盘数字（NumLock 关闭时）
 *   - 会话监听：WTS 会话变更、设备拔出、休眠时触发 Reset 事件
 *   - 光标追踪：每事件附带光标坐标与高精度时间戳
 *
 * 依赖关系:
 *   内部依赖：ih::input（Key、Event、EventType）、ih::render2d::Point、ih::Core::Clock（Now）
 *   外部依赖：Win32 Raw Input API、WTSApi32、User32
 *
 * 注意事项:
 *   隐藏工具窗口仅用于接收消息，不显示
 *   Pause 键特殊处理：额外合成一次 Up 事件（硬件 Pause 无 BREAK 标志）
 *   OpenInputDesktop 检测当前是否为默认桌面（锁屏/安全桌面时不可用）
 *
 ******************************************************************************/
#include "Platform/Windows/Win32Input.h"
#include "Core/Clock.h"
#include <WtsApi32.h>
#include <utility>
#include <stdexcept>
// Win32 输入采集命名空间
namespace ih::platform {
// 将 Win32 虚拟键码映射为逻辑 Key 枚举
// vk: 虚拟键码；scan: 扫描码；flags: Raw Input 标志（含 E0 扩展位）
input::Key MapKey(uint16_t vk, uint16_t scan, uint16_t flags) {
    using input::Key;
    // 解析扩展键标志（E0 前缀用于区分右 Ctrl/Alt 等）
    const bool extended = (flags & RI_KEY_E0) != 0;
    // 忽略异常键码（255 为鼠标移动等非键事件）
    if (vk == 255 || scan == 0xff) return Key::Unknown;
    // 字母键 A-Z：区间线性映射
    if (vk >= 'A' && vk <= 'Z') return static_cast<Key>(input::Index(Key::A) + vk - 'A');
    // 主键盘数字 0-9
    if (vk >= '0' && vk <= '9') return static_cast<Key>(input::Index(Key::Num0) + vk - '0');
    // 功能键 F1-F24
    if (vk >= VK_F1 && vk <= VK_F24) return static_cast<Key>(input::Index(Key::F1) + vk - VK_F1);
    // 小键盘数字
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return static_cast<Key>(input::Index(Key::Numpad0) + vk - VK_NUMPAD0);
    // 其余按键通过 switch 逐一映射
    switch (vk) {
    // Ctrl：扩展键位区分左右
    case VK_CONTROL:return extended ? Key::RightCtrl : Key::LeftCtrl;
    case VK_LCONTROL:return Key::LeftCtrl; case VK_RCONTROL:return Key::RightCtrl;
    // Shift：通过扫描码 0x36 区分右 Shift
    case VK_SHIFT:return scan == 0x36 ? Key::RightShift : Key::LeftShift;
    case VK_LSHIFT:return Key::LeftShift; case VK_RSHIFT:return Key::RightShift;
    // Alt：扩展键位区分左右
    case VK_MENU:return extended ? Key::RightAlt : Key::LeftAlt;
    case VK_LMENU:return Key::LeftAlt; case VK_RMENU:return Key::RightAlt;
    case VK_LWIN:return Key::LeftWin; case VK_RWIN:return Key::RightWin;
    case VK_ESCAPE:return Key::Escape; case VK_TAB:return Key::Tab;
    // Enter：扩展键位区分小键盘 Enter
    case VK_RETURN:return extended ? Key::NumpadEnter : Key::Enter;
    case VK_SPACE:return Key::Space; case VK_BACK:return Key::Backspace;
    // 编辑键区：扩展键位区分功能键与小键盘数字（NumLock 状态下）
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
    // 锁定键与系统键
    case VK_CAPITAL:return Key::CapsLock; case VK_NUMLOCK:return Key::NumLock; case VK_SCROLL:return Key::ScrollLock;
    case VK_SNAPSHOT:return Key::PrintScreen; case VK_PAUSE:return Key::Pause; case VK_APPS:return Key::Menu;
    case VK_ADD:return Key::Add; case VK_SUBTRACT:return Key::Subtract; case VK_MULTIPLY:return Key::Multiply;
    case VK_DIVIDE:return Key::Divide; case VK_DECIMAL:return Key::Decimal;
    // OEM 符号键映射
    case VK_OEM_1:return Key::Semicolon; case VK_OEM_7:return Key::Apostrophe; case VK_OEM_3:return Key::Grave;
    case VK_OEM_MINUS:return Key::Minus; case VK_OEM_PLUS:return Key::Equals; case VK_OEM_4:return Key::LeftBracket;
    case VK_OEM_6:return Key::RightBracket; case VK_OEM_5:case VK_OEM_102:return Key::Backslash;
    case VK_OEM_COMMA:return Key::Comma; case VK_OEM_PERIOD:return Key::Period; case VK_OEM_2:return Key::Slash;
    default:return Key::Unknown;
    }
}
// 析构：移除 Raw Input 注册、注销会话通知、销毁窗口与窗口类
Win32Input::~Win32Input() {
    if (m_window) {
        // 用 RIDEV_REMOVE 注销键盘与鼠标 Raw Input
        const RAWINPUTDEVICE devices[]{{1,6,RIDEV_REMOVE,nullptr},{1,2,RIDEV_REMOVE,nullptr}};
        RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
        // 注销 WTS 会话通知
        WTSUnRegisterSessionNotification(m_window);
        DestroyWindow(m_window);
        // 注销窗口类
        UnregisterClassW(L"InputHalo.RawInput", GetModuleHandleW(nullptr));
    }
}
// 初始化：注册隐藏工具窗口并启用 Raw Input 全局捕获
std::expected<void, std::string> Win32Input::Initialize() {
    // 注册窗口类（仅接收消息，无界面）
    WNDCLASSW definition{}; definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpfnWndProc = Proc; definition.lpszClassName = L"InputHalo.RawInput";
    if (!RegisterClassW(&definition)) return std::unexpected("Cannot register input receiver class");
    // 创建 0 尺寸工具窗口（WS_EX_NOACTIVATE 不抢占焦点）
    m_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, definition.lpszClassName, L"", WS_POPUP,
        0,0,0,0,nullptr,nullptr,definition.hInstance,this);
    if (!m_window) { UnregisterClassW(definition.lpszClassName, definition.hInstance); return std::unexpected("Cannot create input receiver"); }
    // 注册 Raw Input：INPUTSINK 后台接收 + DEVNOTIFY 设备变更通知
    const RAWINPUTDEVICE devices[]{{1,6,RIDEV_INPUTSINK | RIDEV_DEVNOTIFY,m_window}, {1,2,RIDEV_INPUTSINK | RIDEV_DEVNOTIFY,m_window}};
    if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) return std::unexpected("Cannot register Raw Input");
    // 注册会话变更通知（锁屏/解锁/切换用户）
    WTSRegisterSessionNotification(m_window, NOTIFY_FOR_THIS_SESSION);
    return {};
}
// 尝试获取光标屏幕坐标，失败返回 nullopt
std::optional<render2d::Point> Win32Input::TryCursorPosition() {
    POINT point{};if(!GetCursorPos(&point))return std::nullopt;
    return render2d::Point{static_cast<float>(point.x),static_cast<float>(point.y)};
}
// 获取光标坐标，失败返回原点
render2d::Point Win32Input::CursorPosition() { return TryCursorPosition().value_or(render2d::Point{}); }
// 检测当前输入桌面是否为默认桌面
// 锁屏/Ctrl+Alt+Del 安全桌面时返回 false，应暂停采集
bool Win32Input::DesktopAvailable() {
    // 打开输入桌面（只读）
    const auto desktop = OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS);
    if (!desktop) return false;
    wchar_t name[128]{}; DWORD bytes{};
    // 比较桌面名称是否为 Default
    const bool available = GetUserObjectInformationW(desktop, UOI_NAME, name, sizeof(name), &bytes) && wcscmp(name, L"Default") == 0;
    CloseDesktop(desktop); return available;
}
// 查询系统是否禁用了客户端动画（减少动态效果偏好）
// 返回 true 表示用户要求减少动效，应缩短光环动画
bool Win32Input::ReduceMotion() { BOOL enabled = TRUE; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0); return !enabled; }
// 读取一条 Raw Input 消息并转换为输入事件
// handle 为 WM_INPUT 消息携带的 HRAWINPUT
void Win32Input::Read(HRAWINPUT handle) {
    // 获取完整 Raw Input 数据
    RAWINPUT input{}; UINT bytes = sizeof(input);
    if (GetRawInputData(handle, RID_INPUT, &input, &bytes, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return;
    // 获取光标当前位置
    const auto cursor=TryCursorPosition();
    // 光标不可用（安全桌面）时发送 Reset 事件
    if(!cursor){m_events.clear();m_events.push_back({input::EventType::Reset});return;}
    // 记录高精度时间戳与光标坐标
    const auto time = Now(); const auto position = *cursor;
    // 事件队列超限（>4096）则重置，防内存膨胀
    if (m_events.size() > 4096) { m_events.clear(); m_events.push_back({input::EventType::Reset}); }
    // 键盘事件处理
    if (input.header.dwType == RIM_TYPEKEYBOARD) {
        const auto& key = input.data.keyboard;
        // 映射虚拟键到逻辑 Key
        const auto mapped = MapKey(key.VKey, key.MakeCode, key.Flags);
        if (mapped != input::Key::Unknown) {
            // 根据 BREAK 标志判定 Up/Down 事件
            m_events.push_back({(key.Flags & RI_KEY_BREAK) ? input::EventType::Up : input::EventType::Down, mapped, 0, time, position});
            // Pause 键额外合成一次 Up（硬件无 BREAK）
            if (mapped == input::Key::Pause) m_events.push_back({input::EventType::Up, mapped, 0, time, position});
        }
    // 鼠标事件处理
    } else if (input.header.dwType == RIM_TYPEMOUSE) {
        const auto flags = input.data.mouse.usButtonFlags;
        // 五个鼠标按键的逻辑 Key 表
        constexpr input::Key buttons[]{input::Key::MouseLeft,input::Key::MouseRight,input::Key::MouseMiddle,input::Key::Mouse4,input::Key::Mouse5};
        // 遍历按键位掩码，检查按下与释放位
        for (unsigned i = 0; i < 5; ++i) {
            if (flags & (1u << (i * 2))) m_events.push_back({input::EventType::Down,buttons[i],0,time,position});
            if (flags & (2u << (i * 2))) m_events.push_back({input::EventType::Up,buttons[i],0,time,position});
        }
        // 滚轮事件：增量除以 120 为标准化刻度
        if (flags & RI_MOUSE_WHEEL) m_events.push_back({input::EventType::Wheel,{},static_cast<SHORT>(input.data.mouse.usButtonData) / 120.f,time,position});
    }
}
// 窗口过程：静态回调，通过 GWLP_USERDATA 转发到实例
LRESULT CALLBACK Win32Input::Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // 取出实例指针
    auto* self = reinterpret_cast<Win32Input*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    // WM_NCCREATE 时将 this 存入窗口用户数据
    if (message == WM_NCCREATE) { self = static_cast<Win32Input*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (self) try {
        // WM_INPUT：读取原始输入
        if (message == WM_INPUT) self->Read(reinterpret_cast<HRAWINPUT>(lParam));
        // 设备拔出/会话变更/休眠时发送 Reset 事件
        if ((message == WM_INPUT_DEVICE_CHANGE && wParam == GIDC_REMOVAL) || message == WM_WTSSESSION_CHANGE ||
            (message == WM_POWERBROADCAST && wParam == PBT_APMSUSPEND)) {
            self->m_events.clear(); self->m_events.push_back({input::EventType::Reset});
        }
    // 异常时清空事件并标记失败
    } catch (...) { self->m_events.clear(); self->m_failed = true; }
    return DefWindowProcW(window,message,wParam,lParam);
}
}
