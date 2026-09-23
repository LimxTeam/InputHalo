# InputHalo 技术规格书

> 文档类型：Codex / AI 编程代理主规格文档  
> 项目名称：**InputHalo**  
> 内部简称：`IH`  
> 文档版本：`v0.1.0`  
> 日期：2026-09-23  
> 目标平台：Windows 10 / Windows 11 x64  
> 语言标准：C++23  
> 构建系统：CMake 4.4.3  
> 图形 API：Vulkan 1.4.357.0  
> 产品定位：录屏、直播、软件教学场景中的鼠标/键盘输入可视化、鼠标装饰、临时激光笔

---

# 0. AI / Codex 必读

这不是一个通用图形引擎项目，也不是一个 3D 引擎项目。

不要擅自扩展为：

- 3D Renderer
- PBR
- ECS
- Scene
- Asset Browser
- Docking Editor
- Screen Capture
- Magnifier
- Annotation Editor
- Whiteboard
- OCR
- Streaming
- Video Recording
- Plugin System
- Scripting
- Network Service
- Cloud Sync
- Telemetry
- Account System

本项目只有三个核心能力：

1. **鼠标和键盘操作显示**
2. **鼠标视觉装饰**
3. **按住快捷键时出现的临时激光笔 / 激光轨迹**

所有可见应用 UI、Overlay、文字、图形、动画均由项目自己的 Vulkan 2D Renderer 渲染。

禁止使用任何现成 UI 框架。

允许使用第三方底层依赖，但第三方依赖不能替代 UI、Overlay 或核心渲染逻辑。

核心原则：

```text
Small product
Small codebase
Small dependency surface
High visual quality
High input accuracy
Low latency
No feature creep
```

如果某个设计明显是为了未来可能存在的需求，而当前功能不需要，则默认不实现。

---

# 1. 产品名称

## 1.1 名称

**InputHalo**

含义：

- `Input`：键盘、鼠标输入
- `Halo`：鼠标周围的视觉光环、点击波纹、演示高亮

名称能覆盖产品最重要的两类价值：

- Input Visualization
- Cursor Decoration

Laser 是辅助能力，不需要放进产品名。

当前名称视为开发阶段正式工作名。发布前再进行商标、域名和应用商店名称冲突检查。

---

# 2. 产品目标

InputHalo 是一个极简 Windows 桌面工具。

主要用户：

- Unreal Engine / Blender / CAD / IDE 等软件教程作者
- YouTube / Bilibili 教程作者
- 直播教学用户
- 软件演示用户
- 录屏用户

InputHalo 不负责录屏。

它只负责把原本不容易从视频中看清楚的输入动作显示出来。

典型工作流：

```text
用户打开 OBS
    ↓
用户启动 InputHalo
    ↓
InputHalo 在系统桌面上建立透明 Overlay
    ↓
用户操作 Unreal Engine / Blender / Browser
    ↓
InputHalo 显示：
    - 鼠标位置装饰
    - 鼠标左/右/中键操作
    - 滚轮方向
    - 键盘快捷键
    - 临时 Laser
    ↓
OBS 捕获屏幕
```

---

# 3. V1 产品范围

## 3.1 Input Visualizer

必须支持：

### Keyboard

- A-Z
- 0-9
- F1-F24
- Ctrl
- Shift
- Alt
- Win
- Tab
- Caps Lock
- Enter
- Backspace
- Delete
- Insert
- Home
- End
- Page Up
- Page Down
- Escape
- Space
- Arrow Keys
- Numpad
- 常见符号键
- 多键组合

典型显示：

```text
Ctrl + Shift + S
```

UI 不是普通字符串拼接，而是独立 Key Cap：

```text
┌──────┐ ┌───────┐ ┌───┐
│ CTRL │ │ SHIFT │ │ S │
└──────┘ └───────┘ └───┘
```

默认行为：

- Modifier + Key 作为一个 chord 合并显示
- 同一组合键在短时间内重复触发时，不创建大量重复 UI
- Key Up 后开始计时
- 经过 Hold Duration 后进入 Fade Out
- Fade 完成后从 Overlay 删除

建议默认参数：

```text
Hold Duration = 650 ms
Fade Duration = 180 ms
Corner Radius = 7 DIP
Key Height     = 34 DIP
Gap            = 6 DIP
```

### Mouse

显示：

- Left Button
- Right Button
- Middle Button
- Mouse 4
- Mouse 5
- Wheel Up
- Wheel Down

显示形式优先使用视觉化 Mouse Glyph，而不是文本。

例如：

```text
╭─────────╮
│ L │ R   │
│   │     │
│   ▣     │
╰─────────╯
```

按下左键时左侧高亮。

滚轮时滚轮区域出现：

```text
↑
```

或：

```text
↓
```

Mouse Visualizer 与 Keyboard Visualizer 可以位于同一个 Input Bar 中。

---

## 3.2 Cursor Decoration

始终跟随 Windows 系统鼠标当前位置。

V1 只实现 3 种视觉样式：

### Halo

半透明填充圆 + 柔和边缘。

### Ring

透明中心 + 边框圆环。

### Spot

低透明度实心圆。

必须支持：

- Enabled
- Style
- Size
- Color
- Opacity
- Border Width
- Glow Strength
- Click Ripple
- Left Click Color
- Right Click Color

禁止默认添加明显鼠标拖尾。

鼠标装饰必须以真实系统鼠标为中心，不能为了“顺滑”产生明显位置延迟。

原则：

```text
Cursor correctness > ornamental physics
```

不使用 Spring Position Follow。

动画只用于：

- Click Ripple
- Opacity
- Scale
- Hover preview
- Settings UI transitions

---

## 3.3 Click Ripple

鼠标点击时，在点击位置创建一次性 Ripple。

基本模型：

```text
radius:
0.35 → 1.0

opacity:
1.0 → 0.0

duration:
320 ms
```

Ripple 生命周期完全在 GPU Overlay 内完成。

建议：

```text
Left Click:
Accent Color

Right Click:
Secondary Color

Middle Click:
Neutral Color
```

每个 Ripple 必须保存创建时的位置。

即使鼠标已经离开，Ripple 仍然从原点击点扩散。

---

## 3.4 Temporary Laser

不是批注系统。

没有：

- Undo
- Redo
- Rectangle
- Arrow
- Text
- Save
- Export
- History
- Persistent strokes

激活方式：

```text
Hold Hotkey
```

例如默认：

```text
Ctrl + Alt + Space
```

实际快捷键必须可配置。

可以允许 Mouse4 / Mouse5 作为激活键。

Laser 有两种模式：

### Pointer

鼠标附近显示高亮 Laser Dot。

结构：

```text
Outer Glow
Middle Glow
Core Dot
```

### Trail

用户移动鼠标时形成临时轨迹。

轨迹自动消失。

默认：

```text
Line Width    = 5 DIP
Core Opacity  = 1.0
Glow Width    = 14 DIP
Fade Lifetime = 1200 ms
```

Laser 按键松开以后：

- 停止创建新采样点
- 现有轨迹继续自然衰减
- 最后自动删除

Laser 不应该阻塞底层应用的鼠标操作。

Overlay 始终保持 Click-Through。

---

# 4. 明确不做的功能

V1 明确禁止：

```text
Screen Magnifier
Screen Recording
Screenshot
Video Encoder
Whiteboard
Persistent Drawing
Shape Annotation
Text Annotation
Desktop Capture
Remote Control
Presentation Mode
OBS Plugin
Browser Source
Virtual Camera
3D Effects
Particle System
Plugin SDK
Scripting
Cloud
User Account
License Server
Online Update
Telemetry
Crash Upload
AI
```

如果 Codex 在开发中认为某个功能“以后可能有用”，不要自动实现。

必须遵守 YAGNI。

---

# 5. 技术栈

## 5.1 Mandatory

```text
C++23
CMake 4.4.3
Vulkan SDK 1.4.357.0
Windows 10 / 11 x64
MSVC
Win32 API
```

## 5.2 Third-party dependencies

允许：

```text
Volk
Vulkan Memory Allocator
FreeType
HarfBuzz
spdlog
nlohmann/json
```

可选：

```text
Catch2
```

禁止：

```text
Qt
QML
Dear ImGui
WinUI
WPF
MFC UI
wxWidgets
JUCE
CEF
Electron
Chromium
SDL UI
GLFW UI
Skia
NanoVG
RmlUi
Ultralight
Flutter
WebView
React Native
```

SDL / GLFW 也不要用于窗口系统。

窗口直接 Win32。

---

# 6. Build Strategy

根目录：

```text
CMakeLists.txt
CMakePresets.json
```

最低：

```cmake
cmake_minimum_required(VERSION 4.4)

project(InputHalo
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

MSVC：

```text
/W4
/permissive-
/Zc:__cplusplus
/utf-8
/EHsc
```

Debug：

```text
Vulkan Validation Layers = ON
Assertions = ON
Debug Names = ON
```

Release：

```text
Validation Layers = OFF
LTO = ON
Debug Names = optional
```

不要全局使用 `/WX`。

CI 可以单独打开 warnings-as-errors。

---

# 7. Dependency Management

使用 CMake `FetchContent` 或 Git submodule。

必须固定 tag / commit。

禁止：

```text
main
master
latest
HEAD
```

依赖必须可重复构建。

建议：

```text
ThirdParty/
```

只保留许可证和必要补丁。

Vulkan SDK 通过环境：

```text
VULKAN_SDK
```

查找。

---

# 8. 总体架构

```text
┌─────────────────────────────────────────────┐
│                 Application                 │
├─────────────────────────────────────────────┤
│ Settings UI              Runtime Controller │
├────────────────────┬────────────────────────┤
│ UI Runtime         │ Feature Systems        │
│                    │                        │
│ Widget             │ Input Visualizer       │
│ Layout             │ Cursor FX              │
│ Style              │ Laser                  │
│ Animation          │                        │
├────────────────────┴────────────────────────┤
│                Overlay System               │
├─────────────────────────────────────────────┤
│                 Render2D                    │
│ Shape / Text / Stroke / Glow / Clip         │
├─────────────────────────────────────────────┤
│                Vulkan RHI                   │
├─────────────────────────────────────────────┤
│              Windows Platform               │
│ Window / Input / DPI / Monitor / DWM        │
└─────────────────────────────────────────────┘
```

依赖方向必须严格向下。

错误示例：

```text
Renderer2D -> SettingsPanel
```

禁止。

正确：

```text
SettingsPanel -> UI -> Renderer2D -> RHI
```

---

# 9. 工程结构

```text
InputHalo/
│
├── CMakeLists.txt
├── CMakePresets.json
├── LICENSE
├── README.md
│
├── Source/
│   │
│   ├── App/
│   │   ├── Main.cpp
│   │   ├── Application.h
│   │   ├── Application.cpp
│   │   ├── AppState.h
│   │   └── AppState.cpp
│   │
│   ├── Core/
│   │   ├── Assert.h
│   │   ├── Base.h
│   │   ├── Types.h
│   │   ├── Time.h
│   │   ├── Time.cpp
│   │   ├── Math/
│   │   │   ├── Vec2.h
│   │   │   ├── Vec4.h
│   │   │   ├── Rect.h
│   │   │   ├── Color.h
│   │   │   └── Mat3x2.h
│   │   ├── Event/
│   │   ├── Config/
│   │   └── Logging/
│   │
│   ├── Platform/
│   │   └── Windows/
│   │       ├── Win32Application.h
│   │       ├── Win32Application.cpp
│   │       ├── Win32Window.h
│   │       ├── Win32Window.cpp
│   │       ├── Win32OverlayWindow.h
│   │       ├── Win32OverlayWindow.cpp
│   │       ├── Win32Input.h
│   │       ├── Win32Input.cpp
│   │       ├── Win32Monitor.h
│   │       ├── Win32Monitor.cpp
│   │       ├── Win32Dpi.h
│   │       ├── Win32Dpi.cpp
│   │       └── Win32Dwm.h
│   │
│   ├── RHI/
│   │   └── Vulkan/
│   │       ├── VulkanContext.h
│   │       ├── VulkanContext.cpp
│   │       ├── VulkanDevice.h
│   │       ├── VulkanDevice.cpp
│   │       ├── VulkanSwapchain.h
│   │       ├── VulkanSwapchain.cpp
│   │       ├── VulkanBuffer.h
│   │       ├── VulkanBuffer.cpp
│   │       ├── VulkanImage.h
│   │       ├── VulkanImage.cpp
│   │       ├── VulkanPipeline.h
│   │       ├── VulkanPipeline.cpp
│   │       ├── VulkanFrame.h
│   │       └── VulkanFrame.cpp
│   │
│   ├── Render2D/
│   │   ├── Renderer2D.h
│   │   ├── Renderer2D.cpp
│   │   ├── DrawList.h
│   │   ├── DrawList.cpp
│   │   ├── Painter.h
│   │   ├── Painter.cpp
│   │   ├── ShapeRenderer.h
│   │   ├── ShapeRenderer.cpp
│   │   ├── StrokeRenderer.h
│   │   ├── StrokeRenderer.cpp
│   │   ├── TextRenderer.h
│   │   ├── TextRenderer.cpp
│   │   ├── FontAtlas.h
│   │   ├── FontAtlas.cpp
│   │   └── ClipStack.h
│   │
│   ├── UI/
│   │   ├── Widget.h
│   │   ├── Widget.cpp
│   │   ├── UIContext.h
│   │   ├── UIContext.cpp
│   │   ├── Layout/
│   │   ├── Animation/
│   │   ├── Theme/
│   │   └── Widgets/
│   │
│   ├── Overlay/
│   │   ├── OverlayManager.h
│   │   ├── OverlayManager.cpp
│   │   ├── OverlayScene.h
│   │   └── OverlayScene.cpp
│   │
│   ├── Input/
│   │   ├── InputManager.h
│   │   ├── InputManager.cpp
│   │   ├── KeyboardState.h
│   │   ├── MouseState.h
│   │   ├── KeyNames.h
│   │   └── ShortcutAggregator.h
│   │
│   ├── Features/
│   │   ├── InputVisualizer/
│   │   ├── CursorFX/
│   │   └── Laser/
│   │
│   └── Settings/
│       ├── SettingsModel.h
│       ├── SettingsModel.cpp
│       ├── SettingsWindow.h
│       └── Panels/
│
├── Shaders/
│   ├── Shape.hlsl
│   ├── Text.hlsl
│   ├── Stroke.hlsl
│   └── Blur.hlsl
│
├── Assets/
│   └── Fonts/
│
├── Tests/
│
└── ThirdParty/
```

不要创建 `Engine/`。

这是应用，不是通用引擎。

---

# 10. Core Math

不要引入 GLM。

本产品只需要：

```cpp
struct Vec2
{
    float x{};
    float y{};
};

struct Vec4
{
    float x{};
    float y{};
    float z{};
    float w{};
};

struct Rect
{
    float x{};
    float y{};
    float width{};
    float height{};
};

struct Color
{
    float r{};
    float g{};
    float b{};
    float a{};
};
```

Transform2D：

```cpp
struct Mat3x2
{
    float m11, m12;
    float m21, m22;
    float m31, m32;
};
```

不要写泛型数学库。

---

# 11. Application 生命周期

```text
WinMain
  ↓
Initialize Logging
  ↓
Set DPI Awareness
  ↓
Create Message Receiver Window
  ↓
Initialize Input
  ↓
Initialize Vulkan
  ↓
Enumerate Monitors
  ↓
Create Overlay Windows
  ↓
Create Settings Window
  ↓
Load Config
  ↓
Main Loop
  ↓
Save Config
  ↓
Destroy Overlay
  ↓
Destroy Vulkan
  ↓
Shutdown
```

异常初始化必须保证资源逆序释放。

---

# 12. Windows DPI

程序启动第一时间设置：

```text
PER_MONITOR_AWARE_V2
```

不要依赖 Windows 自动缩放。

UI 所有尺寸使用：

```text
DIP
```

渲染前转换：

```text
physicalPx = dip * dpiScale
```

DPI scale：

```text
96 DPI  = 1.0
120 DPI = 1.25
144 DPI = 1.5
192 DPI = 2.0
```

Overlay 鼠标坐标最终必须使用 physical desktop pixel 校准。

---

# 13. Multi-monitor

第一版就正确支持多显示器。

不要把整个 Virtual Desktop 做成一个巨型 HWND。

推荐：

```text
1 monitor
=
1 Overlay HWND
=
1 VkSurfaceKHR
=
1 VkSwapchainKHR
```

共享：

```text
VkInstance
VkPhysicalDevice
VkDevice
Graphics Queue
```

每个显示器 Overlay：

```cpp
struct OverlayTarget
{
    HMONITOR monitor;
    HWND hwnd;
    Rect desktopBoundsPx;
    float dpiScale;

    VkSurfaceKHR surface;
    VulkanSwapchain swapchain;
};
```

鼠标位置：

```text
GetCursorPos()
      ↓
Physical desktop coordinate
      ↓
Find monitor
      ↓
Convert to monitor local coordinate
      ↓
Draw Cursor FX
```

Input Visualizer 默认显示在：

```text
Active Cursor Monitor
```

设置中可以增加：

```text
Overlay Monitor:
- Follow Cursor
- Primary Monitor
- Monitor 1
- Monitor 2
```

---

# 14. Overlay Window

Overlay 必须：

```text
Borderless
Topmost
No taskbar icon
No activation
Click-through
Transparent
```

目标 extended styles：

```text
WS_EX_TOPMOST
WS_EX_TOOLWINDOW
WS_EX_NOACTIVATE
WS_EX_TRANSPARENT
```

透明合成不要简单依赖传统 GDI Color Key。

Vulkan Swapchain 创建前必须查询：

```cpp
VkSurfaceCapabilitiesKHR::supportedCompositeAlpha
```

优先：

```text
VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR
```

其次：

```text
VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR
```

再其次：

```text
VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
```

Render Target 清屏：

```text
RGBA = 0,0,0,0
```

UI / Overlay 所有颜色建议内部使用 Premultiplied Alpha。

Blend：

```text
srcColor = ONE
dstColor = ONE_MINUS_SRC_ALPHA
```

对 Win32 / DWM 透明合成单独创建兼容层。

不要假设所有驱动支持完全相同的 composite alpha 模式。

必须：

```text
query
select
log
```

如果目标机器只有不满足 Overlay 的组合模式：

- 不崩溃
- Settings Window 正常启动
- 显示 Overlay Compatibility Error
- 输出日志

不要静默显示黑色 Overlay。

---

# 15. Settings Window

Settings Window 与 Overlay 是两套 Window Policy。

Settings：

```text
Normal desktop app window
Resizable
Minimizable
Closable to tray optional
```

视觉上采用自绘 title bar。

禁止调用系统 Button / Slider / ComboBox 控件。

Win32 只负责：

```text
HWND
Input
IME
Window resize
DPI
System commands
```

窗口内部像素全部由 Vulkan 渲染。

---

# 16. Vulkan RHI 范围

不要构建通用 RHI 抽象层。

项目只有 Vulkan。

因此命名可以是：

```text
VulkanContext
VulkanBuffer
VulkanImage
VulkanSwapchain
```

而不是：

```text
IRenderDevice
IGraphicsAPI
IDeviceBackend
```

不要为了不存在的 D3D12 / Metal 做接口。

RHI 只覆盖实际需要：

```text
Instance
Device
Queue
Surface
Swapchain
Command Pool
Command Buffer
Fence
Semaphore
Buffer
Image
Image View
Sampler
Pipeline
Pipeline Layout
Descriptor Set
Descriptor Pool
```

---

# 17. Vulkan Feature Baseline

目标 Vulkan：

```text
1.4
```

使用现代路径：

```text
Dynamic Rendering
Synchronization2
Timeline Semaphore optional
```

但不要使用用不到的高级特性：

```text
Ray Tracing
Mesh Shader
Descriptor Buffer
Meshlets
Bindless Renderer
GPU Driven Rendering
```

本项目没有意义。

---

# 18. Frames in Flight

默认：

```text
2 frames in flight
```

原因：

- 降低延迟
- Overlay 不需要吞吐型 renderer
- UI 画面简单

不要默认 3 帧。

---

# 19. Presentation

优先：

```text
VK_PRESENT_MODE_MAILBOX_KHR
```

不可用时：

```text
VK_PRESENT_MODE_FIFO_KHR
```

不要使用 Immediate 作为默认。

Settings 和 Overlay 可以有不同 present strategy。

InputHalo 的核心优先级：

```text
Low latency
Stable frame pacing
No tearing
```

---

# 20. Render2D

## 20.1 设计原则

UI 和 Overlay 共用 Renderer2D。

API 示例：

```cpp
Painter painter;

painter.RoundedRect(rect, radius, color);
painter.Circle(center, radius, color);
painter.Line(a, b, width, color);
painter.Text(font, text, position, style);
```

Painter 不直接执行 Vulkan API。

Painter 生成：

```text
DrawList
```

Renderer2D 消费 DrawList。

---

# 21. DrawList

```cpp
enum class PrimitiveType : uint8_t
{
    Rect,
    RoundedRect,
    Circle,
    Line,
    Text,
    Image
};
```

不要为每种 primitive 创建复杂继承树。

使用 POD command。

例如：

```cpp
struct RoundedRectCommand
{
    Rect bounds;
    Color fill;
    Color border;
    float radius;
    float borderWidth;
};
```

---

# 22. GPU Shape Rendering

主要 UI 图形使用解析 SDF。

不要 CPU tessellate 每个圆角矩形。

Rounded Rect Fragment Shader 通过 signed distance 计算：

```text
distance to rounded rectangle
```

产生：

```text
fill
border
anti-alias
```

Circle 同理。

优势：

- 4K 清晰
- 任意缩放
- 少量 geometry
- Radius 动画便宜

---

# 23. Anti-aliasing

使用：

```text
fwidth
smoothstep
```

基于屏幕导数做解析抗锯齿。

不需要 MSAA 作为 UI 主抗锯齿方案。

Swapchain 可以保持：

```text
1x
```

---

# 24. Glow

Glow 不要做昂贵全屏 Bloom。

Cursor 和 Laser 的 Glow 用局部解析方式实现。

例如 Circle：

```text
core distance
+
falloff(distance)
```

Laser：

```text
core stroke
+
outer stroke with smooth falloff
```

不要为了几个发光元素建立全屏 Blur Pass。

Settings Popup Shadow 可使用解析软阴影或简单局部 blur。

---

# 25. Text

字体栈：

```text
FreeType
+
HarfBuzz
+
Vulkan Font Atlas
```

虽然 V1 UI 以英文为主，内部必须使用 UTF-8。

不要把文本系统写成 ASCII only。

Font Atlas：

```text
R8_UNORM
```

或：

```text
R8
```

小字号优先普通灰度 glyph。

不要在 V1 引入 MSDF Font Pipeline，除非灰度 Atlas 在高 DPI 下确实达不到质量要求。

UI 字体建议：

```text
Inter
```

产品发布时字体必须遵守许可证。

不要把字体文件硬编码成开发机路径。

---

# 26. Font Atlas

第一版：

```text
Dynamic Atlas
2048 x 2048
```

需要时扩容或创建第二页。

Glyph key：

```cpp
struct GlyphKey
{
    FontId font;
    uint32_t glyphIndex;
    uint16_t pixelSize;
};
```

Text Layout 使用 HarfBuzz 输出 glyph indices 和 offsets。

---

# 27. UI System

不要重新造 Qt。

本项目 Settings UI 控件很少。

选择轻量 retained-mode。

```text
UIContext
  ↓
Root Widget
  ↓
Layout
  ↓
Hit Test
  ↓
Paint
  ↓
DrawList
```

Widget 仅实现项目需要的能力。

---

# 28. Required Widgets

只做：

```text
Panel
Text
Button
IconButton
Toggle
Slider
ComboBox
ColorPicker
HotkeyInput
TabButton
ScrollView
Separator
Tooltip
```

不要做：

```text
TreeView
Table
DataGrid
Dock
RichText
CodeEditor
GraphEditor
Timeline
Canvas Editor
```

---

# 29. UI Layout

只需要：

```text
VerticalStack
HorizontalStack
Overlay
Padding
Fixed
Flexible
```

可以实现一个小型 Flex 思想，但不要实现完整 CSS。

Layout 过程：

```text
Measure
Arrange
Paint
```

---

# 30. UI Event Model

事件：

```text
PointerMove
PointerDown
PointerUp
Wheel
KeyDown
KeyUp
TextInput
Focus
Blur
```

传播：

```text
Hit Test
  ↓
Target Widget
```

V1 不需要 DOM 级 capture/bubble。

除非实际控件需求出现，否则保持简单。

---

# 31. UI Animation

统一 Timeline / Tween。

支持：

```text
float
Vec2
Color
```

Easing：

```text
Linear
EaseOutCubic
EaseInOutCubic
```

足够。

不要先实现通用 Spring Animation Framework。

UI animation duration：

```text
Hover       90 ms
Pressed     70 ms
Toggle      140 ms
Popup       160 ms
Page change 180 ms
```

Respect:

```text
Windows Reduce Motion
```

如果系统开启减少动画，则缩短或关闭非必要动画。

---

# 32. Design System

视觉目标：

```text
Modern
Dark
Quiet
Professional
Compact
Not gamer
Not neon overload
Not debug-tool
```

默认 Theme：

```text
Background     #0D0F12
Surface        #13161B
SurfaceRaised  #181C22
SurfaceHover   #1D2229
Border         rgba(255,255,255,0.075)

TextPrimary    rgba(255,255,255,0.92)
TextSecondary  rgba(255,255,255,0.60)
TextDisabled   rgba(255,255,255,0.32)

Accent         #75E6C8
Danger         #FF6B6B
```

Accent 可以设置。

不要用大面积 Accent 背景。

---

# 33. Radius

```text
XS = 4
S  = 6
M  = 8
L  = 12
XL = 16
```

默认：

```text
Button = 7
Input  = 7
Panel  = 10
Popup  = 12
```

---

# 34. Spacing

使用 4px base grid：

```text
4
8
12
16
20
24
32
```

不要随意出现：

```text
13
17
19
23
```

除非图形视觉补偿。

---

# 35. Typography

建议：

```text
Title      20
Section    14 semibold
Body       13
Secondary  12
KeyCap     12 semibold
```

所有字号单位为 DIP。

---

# 36. Settings 页面

只做 4 页：

```text
General
Input
Cursor
Laser
```

---

# 37. General

设置：

```text
Run at startup
Start minimized
Overlay monitor
Interface scale
Accent color
```

可选：

```text
Minimize to tray
```

如果实现 tray：

- 使用 Win32 Shell Notification Icon
- 不建立额外 UI 框架

---

# 38. Input Settings

```text
Keyboard Visualizer
    Enabled
    Show shortcuts only
    Position
    Scale
    Hold duration
    Opacity

Mouse Visualizer
    Enabled
    Position
    Scale
    Show wheel
    Show side buttons
```

`Show shortcuts only` 默认：

```text
ON
```

定义：

显示以下输入：

```text
Modifier + key
Function keys
Navigation keys
Escape
Enter
Backspace
Delete
Tab
```

普通连续文字：

```text
a
b
c
...
```

默认不显示。

用户可以关闭 shortcut-only。

---

# 39. Cursor Settings

```text
Enabled
Style
Size
Color
Opacity
Border width
Glow
Click ripple
Left click color
Right click color
```

Settings 中必须提供实时 Preview。

Preview 在设置窗口内部渲染，不需要真的移动 Overlay Mouse。

---

# 40. Laser Settings

```text
Enabled
Activation hotkey
Mode
Color
Width
Glow
Fade duration
```

Mode：

```text
Pointer
Trail
```

---

# 41. Input Architecture

## 41.1 不使用全局 Keyboard Hook 作为首选

优先：

```text
Raw Input
```

创建独立的隐藏 message receiver HWND。

注册：

```text
Keyboard
Mouse
```

使用：

```text
RIDEV_INPUTSINK
```

即使 Settings Window 没有焦点，也能接收输入。

---

# 42. Keyboard State

```cpp
struct KeyState
{
    bool down;
    uint64_t downTimestamp;
    uint64_t upTimestamp;
};
```

统一内部 key enum：

```cpp
enum class Key : uint16_t
{
    Unknown,

    A,
    B,
    C,

    Num0,

    LeftCtrl,
    RightCtrl,
    LeftShift,
    RightShift,
    LeftAlt,
    RightAlt,
    LeftWin,
    RightWin,

    Escape,
    Tab,
    Enter,
    Space,

    // ...
};
```

不要在 Feature 层传播裸 Win32 virtual-key code。

转换只发生于：

```text
Win32Input
```

---

# 43. Shortcut Aggregator

Keyboard overlay 不应该直接显示原始 event stream。

需要：

```text
Raw Key Events
      ↓
Keyboard State
      ↓
Shortcut Aggregator
      ↓
Display Chord
```

Display Chord：

```cpp
struct ShortcutChord
{
    std::vector<Key> modifiers;
    Key primary;
    uint64_t timestamp;
};
```

排序固定：

```text
Ctrl
Alt
Shift
Win
Primary
```

或者：

```text
Ctrl
Shift
Alt
Win
Primary
```

整个项目统一一种。

建议：

```text
Ctrl
Shift
Alt
Win
```

---

# 44. Repeat

Windows Key Repeat 不应该创建几十个 overlay item。

规则：

如果：

```text
same chord
within 250ms
```

刷新当前显示 item 的 lifetime。

可以在角落显示：

```text
×2
×3
```

V1 可不显示 repeat count。

---

# 45. Mouse State

Raw Input 用于：

```text
Button Down
Button Up
Wheel
Side Buttons
```

鼠标屏幕坐标不要通过 Raw Mouse Delta 累积。

直接：

```text
GetCursorPos()
```

理由：

- 精确匹配 Windows Cursor
- 不受 pointer acceleration 累积误差影响
- 多屏坐标正确
- 不需要自己重建系统 cursor movement

---

# 46. Input Privacy

InputHalo 不能成为 Keylogger。

绝对规则：

```text
Never persist key history
Never upload key history
Never write key events to log
Never write typed characters to disk
Never expose key history through telemetry
```

Debug 日志也不能记录：

```text
Pressed A
Pressed Password
```

只允许记录：

```text
Input subsystem initialized
RawInput device connected
```

所有可视化事件只存在 RAM。

显示结束立即释放。

---

# 47. Secure Desktop

当 Windows 进入安全桌面，例如 UAC / Ctrl+Alt+Del：

InputHalo 不应试图绕过。

Overlay 正常被系统隔离。

不要开发任何：

```text
secure desktop hook
elevation bypass
credential field interception
```

---

# 48. Password Input

V1 不要求建立复杂 UI Automation 密码检测系统。

默认通过：

```text
Shortcut-only mode
```

降低普通文本泄露风险。

未来若增加：

```text
UI Automation IsPassword
```

必须单独作为独立任务。

不要在 V1 擅自做。

---

# 49. Cursor FX State

```cpp
struct CursorFxState
{
    Vec2 positionPx;
    bool leftDown;
    bool rightDown;
    bool middleDown;
};
```

每帧读取：

```text
GetCursorPos
```

不要插值实际 Halo 中心。

---

# 50. Click Ripple Data

```cpp
struct ClickRipple
{
    Vec2 desktopPositionPx;
    Color color;
    float ageSeconds;
    float durationSeconds;
};
```

更新：

```cpp
age += dt;
```

normalized：

```text
t = clamp(age / duration, 0, 1)
```

radius：

```text
lerp(startRadius, endRadius, easeOutCubic(t))
```

alpha：

```text
1 - smoothstep(0, 1, t)
```

---

# 51. Laser Trail

不要把 Laser 当普通 annotation stroke。

Laser 是时间序列。

```cpp
struct LaserPoint
{
    Vec2 desktopPositionPx;
    float creationTime;
};
```

Laser Trail：

```cpp
class LaserTrail
{
    std::deque<LaserPoint> points;
};
```

每帧删除：

```text
now - creationTime > lifetime
```

的点。

---

# 52. Laser Sampling

不是每帧无条件添加。

添加条件：

```text
distance(lastPoint, cursor) >= minimumDistance
OR
timeSinceLastPoint >= maximumInterval
```

建议：

```text
minimumDistance = 1.5 physical px
maximumInterval = 8 ms
```

根据 DPI / refresh 做调优。

必须控制点数量。

例如：

```text
max 4096 points
```

超过时删除最旧点。

---

# 53. Laser Smoothing

V1 使用 CPU 几何 smoothing 即可。

推荐：

```text
Catmull-Rom
```

或：

```text
Quadratic midpoint interpolation
```

不要实现复杂笔刷 engine。

生成 line strip 所需 segment。

Join：

```text
Round
```

Cap：

```text
Round
```

---

# 54. Laser Alpha

每个 point 的 alpha 根据 age：

```text
ageNormalized =
(now - point.creationTime) / lifetime
```

使用：

```text
alpha = 1 - smoothstep(0.55, 1.0, ageNormalized)
```

这样尾部不会突然消失。

---

# 55. Laser Visual

建议两层 stroke：

```text
Outer Glow
Core Stroke
```

例如：

```text
Core Width  = 4 DIP
Glow Width  = 12 DIP
Glow Alpha  = 0.22
Core Alpha  = 0.95
```

默认 Laser 不要做廉价纯红。

可以默认：

```text
Warm Coral / Red
```

但允许用户选颜色。

---

# 56. Overlay Scene

每个 OverlayFrame：

```text
Begin Overlay
    ↓
Cursor Decoration
    ↓
Click Ripples
    ↓
Laser
    ↓
Input Visualizer
End Overlay
```

建议 z-order：

```text
Cursor Decoration = 100
Click Ripple      = 110
Laser             = 120
Input Visualizer  = 200
```

---

# 57. Input Visualizer Positioning

Anchor：

```text
Bottom Left
Bottom Center
Bottom Right
Top Left
Top Center
Top Right
```

用户可设置 offset。

保存：

```text
anchor
offsetDip
```

不要保存绝对 pixel position。

这样 DPI / resolution 变化后不会跑掉。

---

# 58. Settings Config

文件：

```text
%LOCALAPPDATA%/InputHalo/settings.json
```

示例：

```json
{
  "version": 1,
  "general": {
    "run_at_startup": false,
    "start_minimized": false,
    "overlay_monitor": "follow_cursor",
    "ui_scale": 1.0,
    "accent": "#75E6C8"
  },
  "input": {
    "keyboard_enabled": true,
    "shortcut_only": true,
    "mouse_enabled": true,
    "anchor": "bottom_center",
    "offset_x": 0,
    "offset_y": 48,
    "scale": 1.0,
    "hold_ms": 650,
    "fade_ms": 180
  },
  "cursor": {
    "enabled": true,
    "style": "ring",
    "size": 42,
    "color": "#75E6C8",
    "opacity": 0.82,
    "border_width": 2.0,
    "glow": 0.25,
    "click_ripple": true
  },
  "laser": {
    "enabled": true,
    "mode": "trail",
    "hotkey": "Ctrl+Alt+Space",
    "color": "#FF5A5F",
    "width": 5.0,
    "glow": 0.35,
    "fade_ms": 1200
  }
}
```

Config Loader 必须：

- 缺字段使用默认值
- 未知字段忽略
- 非法值 clamp
- JSON 损坏时备份旧文件
- 恢复默认配置
- 不崩溃

---

# 59. Config Version

```text
"version": 1
```

升级时使用 migration。

不要直接假设未来 schema 一样。

但 V1 只需要：

```text
if version == 1
```

不要先写复杂 migration framework。

---

# 60. Threading

V1 不需要复杂 Job System。

建议：

### Main / UI Thread

负责：

```text
Win32 message pump
Settings state
Feature update
Overlay composition
Frame scheduling
```

### Render

第一版可以同线程。

不要过早拆 dedicated render thread。

理由：

- UI 很轻
- Overlay 很轻
- 多线程 Vulkan 增加同步复杂度
- 低延迟比吞吐重要

### Optional Font Worker

字体预热阶段可以同步。

V1 不需要 worker thread。

---

# 61. Main Loop

不要使用：

```text
while(true) { render(); }
```

无条件烧 GPU。

Overlay 需要高响应，但可以做合理节流。

策略：

如果：

```text
Cursor decoration enabled
```

则按照 monitor refresh rate 连续渲染。

如果 Cursor Decoration 关闭，且没有 Laser / animation：

```text
event-driven / low-frequency
```

Settings Window 只有动画时高帧率。

---

# 62. Frame Clock

使用：

```text
QueryPerformanceCounter
```

所有动画使用真实 elapsed time。

不要按 frame count 动画。

---

# 63. Performance Targets

目标机器：

```text
Intel / AMD integrated graphics
NVIDIA consumer GPU
AMD consumer GPU
```

目标：

```text
Overlay GPU frame < 0.30 ms typical
Overlay CPU frame < 0.50 ms typical

Settings GPU frame < 1.0 ms

Idle CPU usage < 1%
Idle GPU usage 尽可能低
```

Cursor Decoration 开启时允许 GPU 持续活动，但必须轻量。

---

# 64. Memory Target

运行内存：

```text
< 150 MB
```

理想：

```text
< 80 MB
```

GPU Memory：

```text
< 128 MB
```

不要建立巨大 Atlas / Framebuffer。

---

# 65. Startup

目标：

```text
cold startup < 1.5 s
```

不需要 splash screen。

---

# 66. Renderer Resource Lifetime

采用明确 ownership。

例如：

```cpp
class VulkanSwapchain
{
public:
    VulkanSwapchain(...);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
};
```

可以使用 RAII。

不要创建一个通用智能句柄系统。

---

# 67. Error Handling

Expected failure：

```text
Vulkan unsupported
No compatible GPU
Swapchain failed
Overlay alpha unsupported
Font missing
Settings JSON damaged
```

不能：

```text
std::terminate()
```

除非真正不可恢复内部 invariant。

用户错误显示设置窗口中的 error state。

日志：

```text
%LOCALAPPDATA%/InputHalo/logs/
```

不记录用户输入内容。

---

# 68. Vulkan Validation

Debug：

开启：

```text
VK_LAYER_KHRONOS_validation
```

并使用：

```text
VK_EXT_debug_utils
```

所有资源赋 debug name：

```text
InputHalo.OverlaySwapchain
InputHalo.ShapePipeline
InputHalo.FontAtlas
```

---

# 69. Shader Strategy

使用：

```text
HLSL
+
DXC
+
SPIR-V
```

Shaders 不是 runtime 编译。

CMake build step 编译：

```text
.hlsl
    ↓
.spv
```

生产包只需要 SPIR-V。

如果 DXC 环境不可用，可以使用 Vulkan SDK 提供的 shader toolchain；但整个项目统一一种路径，不要混用 GLSL/HLSL。

---

# 70. Shader Scope

只允许：

```text
Shape
Text
Stroke
```

Blur 如果后续确实需要再加。

不要建立：

```text
ShaderGraph
ShaderReflection Framework
Hot Reload Server
Material System
```

---

# 71. Shape Pipeline

建议一个 unit quad：

```text
[-1,-1]
[ 1,-1]
[ 1, 1]
[-1, 1]
```

每实例传：

```text
bounds
color
radius
border
type
```

使用 instanced rendering。

---

# 72. Batching

目标不是极限 benchmark。

但避免：

```text
1 Button = 1 vkCmdDraw
```

批次按照：

```text
Pipeline
Texture
Scissor
```

分组。

典型 UI 一帧：

```text
< 20 draw calls
```

Overlay：

```text
< 10 draw calls
```

---

# 73. Clipping

Settings UI 需要 Scissor。

维护：

```text
ClipStack
```

每个 ScrollView push clip。

不需要 stencil clipping。

如果未来 ColorPicker 需要圆形 clip，再单独处理。

---

# 74. Color Space

Swapchain 优先：

```text
VK_FORMAT_B8G8R8A8_SRGB
```

如果不支持，再选择兼容格式。

颜色输入以 sRGB 表示。

Shader / blending 必须确认 alpha 合成行为。

Overlay 透明度必须在 NVIDIA / AMD / Intel 上分别测试。

---

# 75. Modern UI Details

必须避免：

```text
大量 1px 高亮描边
高饱和霓虹色
巨大阴影
大面积毛玻璃
游戏启动器风
Windows 7 风
Debug UI 风
```

Settings 应该像现代专业工具：

```text
dense
quiet
precise
```

交互：

- Hover 轻微亮度变化
- Active 用 Accent
- Focus 明确但克制
- Disabled 明显灰化
- Slider thumb 不宜过大
- Toggle 短动画
- Tooltip 延迟 350 ms

---

# 76. Settings Window Suggested Layout

尺寸参考：

```text
760 x 560 DIP
```

结构：

```text
┌──────────────────────────────────────────────────┐
│ InputHalo                                  — □ × │
├──────────────┬───────────────────────────────────┤
│ General      │                                   │
│ Input        │              Panel                │
│ Cursor       │                                   │
│ Laser        │                                   │
│              │                                   │
└──────────────┴───────────────────────────────────┘
```

左栏：

```text
156 DIP
```

右侧：

```text
flex
```

---

# 77. Custom Titlebar

如果实现自绘 Titlebar：

Win32 必须正确处理：

```text
WM_NCHITTEST
WM_GETMINMAXINFO
WM_DPICHANGED
WM_SIZE
WM_CLOSE
```

支持：

```text
drag
minimize
maximize/restore
close
resize border
```

系统窗口阴影和圆角优先使用 DWM。

不要自己绘制窗口外阴影。

---

# 78. Input Hotkey Recording Widget

用户点击 HotkeyInput：

```text
Recording...
```

然后按：

```text
Ctrl + Alt + Space
```

显示：

```text
Ctrl + Alt + Space
```

禁止使用以下危险组合做默认：

```text
Ctrl + Alt + Delete
Win + L
Alt + F4
```

冲突不需要系统级扫描。

只需要：

- 检测与软件其他 hotkey 冲突
- 提醒用户常见系统快捷键冲突

---

# 79. Startup Registration

Run at startup 使用：

```text
HKCU
```

或 Windows Startup 注册方式。

不需要管理员权限。

InputHalo 整个产品必须默认：

```text
non-elevated
```

不要请求 UAC。

---

# 80. Tray

Tray 是可选。

如果实现：

- 左键打开 Settings
- 右键菜单：
  - Open
  - Enable Overlay
  - Exit

Tray Context Menu 可以先使用 Win32 native menu，因为它属于系统 shell 边界，不属于主应用 UI。

如果要严格全自绘，可以后续改。

V1 可以直接不做 tray。

---

# 81. App State

```cpp
struct AppState
{
    bool overlayEnabled;
    bool settingsVisible;

    InputState input;
    CursorFxState cursor;
    LaserState laser;

    SettingsModel settings;
};
```

不要做全局 Singleton 满天飞。

建议：

```text
Application owns subsystems
```

Subsystem 构造顺序明确。

---

# 82. Logging

spdlog channels：

```text
App
Platform
Vulkan
Overlay
Input
Config
```

禁止：

```text
log every WM_MOUSEMOVE
log every key
log every frame
```

否则日志污染且泄露输入。

---

# 83. Testing

## Unit

测试：

```text
Color parse
Config clamp
Config default
Key mapping
Shortcut aggregation
Anchor positioning
DPI conversion
Laser lifetime
Ripple lifetime
Math
```

## Manual Graphics

必须人工验证：

```text
100% DPI
125% DPI
150% DPI
200% DPI

1080p
1440p
4K

single monitor
dual monitor
mixed DPI

NVIDIA
AMD
Intel
```

---

# 84. Functional Acceptance

## Input

通过：

- Ctrl+C 显示一次
- Ctrl+Shift+S 顺序正确
- Repeat 不疯狂刷屏
- Wheel Up / Down 正确
- Mouse4 / Mouse5 正确
- Settings 失去焦点后仍工作

## Cursor

通过：

- 4K 下仍锐利
- Halo 与实际 Cursor 中心一致
- 多屏跨屏无跳变
- Click Ripple 从点击点发出
- Ripple 不跟随鼠标移动

## Laser

通过：

- Hold hotkey 激活
- Release 停止
- Trail 平滑
- Trail 自动消失
- 不拦截下方应用点击
- 快速移动无明显断裂

---

# 85. Visual Acceptance

设置窗口不能像：

```text
ImGui skin
Debug panel
Game cheat menu
Engineering tool
```

必须满足：

- 字体清晰
- Alignment 一致
- Padding 一致
- Radius 统一
- Slider / Toggle 状态一致
- Hover 动画克制
- 4K 高 DPI 无糊字
- 无明显 aliasing
- Overlay 不出现黑色背景

---

# 86. Stability Acceptance

连续运行：

```text
8 hours
```

期间：

- 不崩溃
- 不持续增长内存
- 不持续增长 descriptor
- 不持续增长 Laser Points
- Swapchain resize 正常
- Monitor disconnect / reconnect 不崩溃
- Sleep / Resume 不崩溃

---

# 87. Device Lost

V1 遇到：

```text
VK_ERROR_DEVICE_LOST
```

不要求复杂热恢复。

正确行为：

```text
log
notify
graceful exit
```

不要无限 retry。

---

# 88. Monitor Change

监听：

```text
WM_DISPLAYCHANGE
WM_DPICHANGED
```

重新：

```text
Enumerate monitors
Recreate affected overlay targets
```

不要每帧 enumerate monitors。

---

# 89. Swapchain Resize

Settings Window resize：

```text
WM_SIZE
    ↓
mark dirty
    ↓
next frame recreate
```

不要在 WM_SIZE 里直接长时间阻塞。

Overlay 随 Monitor geometry 变化重建。

---

# 90. Architecture Boundaries

必须保持：

```text
Platform
    knows Win32

RHI
    knows Vulkan

Render2D
    knows RHI

UI
    knows Render2D

Features
    know UI/Overlay abstractions

Settings
    knows Feature config
```

禁止：

```text
CursorFX directly calls vkCmdDraw
SettingsPanel directly calls Win32 Raw Input
VulkanSwapchain reads settings.json
```

---

# 91. Naming Convention

Types：

```text
PascalCase
```

Functions：

```text
PascalCase
```

Variables：

```text
camelCase
```

Private members：

```text
m_name
```

Constants：

```text
kDefaultFadeMs
```

Namespaces：

```text
ih
ih::platform
ih::render
ih::ui
```

---

# 92. Headers

原则：

- `.h` 只暴露必要接口
- 避免巨型 umbrella header
- 避免 PCH 藏依赖问题

可以使用：

```text
Source/Pch.h
```

但核心 header 仍需 self-contained。

---

# 93. C++23

可以使用：

```text
std::expected
std::span
std::string_view
std::chrono
std::filesystem
std::optional
std::variant
```

不要为了“现代”使用复杂模板元编程。

错误返回优先：

```cpp
std::expected<T, Error>
```

在初始化和 IO 边界尤其适合。

---

# 94. Memory

大多数对象：

```text
stack
unique_ptr
vector
deque
```

不要写 custom allocator。

Vulkan GPU memory 用 VMA。

---

# 95. No Exceptions Across Core Runtime Boundaries

建议：

- 第三方库异常可以在边界捕获
- Core Runtime 尽量显式 expected/error
- 构造阶段不要偷偷吞异常

不要求全项目 `-fno-exceptions`。

MSVC 保持正常异常模型。

---

# 96. Milestone 0 — Skeleton

目标：

```text
CMake builds
WinMain runs
Logging works
Settings JSON default
```

验收：

```text
InputHalo.exe launches
blank Win32 window
clean exit
```

不要开始 UI。

---

# 97. Milestone 1 — Vulkan Foundation

实现：

```text
Instance
Validation
Physical Device
Logical Device
Queue
Surface
Swapchain
Command Buffer
Frame sync
```

Settings Window：

```text
clear solid color
```

验收：

```text
resize stable
minimize stable
restore stable
validation clean
```

---

# 98. Milestone 2 — Transparent Overlay

实现：

```text
Overlay HWND
per-monitor
transparent clear
click-through
topmost
```

先只画一个圆。

验收：

```text
桌面完全透明
圆可见
鼠标能点击圆下面的软件
```

这是整个项目最关键的技术验证点。

在这个 milestone 没通过前，不要写完整 UI。

---

# 99. Milestone 3 — Renderer2D

实现：

```text
Rect
RoundedRect
Circle
Line
Scissor
Basic batching
```

验收：

```text
100/125/150/200% DPI clean
```

---

# 100. Milestone 4 — Text

实现：

```text
FreeType
HarfBuzz
Atlas
Text layout
Text rendering
```

验收：

```text
ASCII
Unicode
high DPI
```

---

# 101. Milestone 5 — Input

实现：

```text
Hidden receiver window
Raw Input
Keyboard mapping
Mouse buttons
Wheel
GetCursorPos
```

先做 debug overlay，不写最终 UI。

验收：

```text
后台运行仍能收到输入
不需要管理员权限
```

---

# 102. Milestone 6 — Cursor FX

实现：

```text
Ring
Halo
Spot
Click Ripple
```

这时开始实际 dogfood。

验收：

```text
OBS screen capture 可正常录到
```

---

# 103. Milestone 7 — Keyboard / Mouse Visualizer

实现：

```text
Chord aggregator
KeyCaps
Mouse glyph
Fade lifecycle
Anchoring
```

验收：

```text
Unreal Engine 操作录屏测试
```

---

# 104. Milestone 8 — Laser

实现：

```text
Hotkey hold
Pointer
Trail
Smoothing
Glow
Fade
```

验收：

```text
fast movement
slow movement
4K
multi-monitor
```

---

# 105. Milestone 9 — Settings UI

实现：

```text
Layout
Button
Toggle
Slider
ComboBox
HotkeyInput
ColorPicker
ScrollView
Tabs
```

完成四页：

```text
General
Input
Cursor
Laser
```

---

# 106. Milestone 10 — Polish

处理：

```text
DPI
multi-monitor
sleep/resume
display reconnect
GPU compatibility
keyboard layouts
animation
startup
logging
packaging
```

---

# 107. Codex 开发规则

Codex 每次开始一个 Milestone 时：

1. 先阅读本规格书对应章节。
2. 查看现有代码，不假设不存在的接口。
3. 不一次完成多个 Milestone。
4. 先定义最小 public API。
5. 实现。
6. 编译。
7. 修复所有 compile error。
8. 运行可执行程序。
9. 检查 Vulkan Validation。
10. 补测试。
11. 只在成功后进入下一步。

---

# 108. Codex 禁止行为

禁止：

```text
为了快速出 UI 引入 ImGui
为了快速建窗口引入 GLFW
为了方便字体改用 Qt
为了透明窗口改用 Electron
为了输入改成 Python helper
为了设置页面嵌 WebView
```

也禁止：

```text
先写一个临时架构以后再重构
```

项目规模足够小，应从第一版保持干净。

---

# 109. Codex 遇到不确定技术点

优先原则：

```text
Verify first
Implement second
```

特别是：

```text
Win32 + Vulkan transparent swapchain
DWM alpha
multi-monitor DPI
Raw Input
present mode
```

需要写最小实验时：

```text
Tools/Experiments/
```

验证成功以后才合并到产品代码。

实验代码不要直接污染正式模块。

---

# 110. 第一个技术 Spike

Codex 第一优先级不是 Settings UI。

是：

> 验证 Windows 11 + Vulkan 是否能够稳定实现 per-pixel alpha transparent, topmost, click-through overlay。

最小程序：

```text
透明桌面
+
一个 Vulkan 圆
```

必须验证：

```text
NVIDIA
AMD
Intel
```

至少开发机先通过。

只有这个实验成功，项目技术路线才算成立。

---

# 111. Vulkan Overlay Fallback 思想

不要在第一版直接实现其他图形 API fallback。

但架构要允许：

```text
Render2D produces transparent RGBA
```

如果某些 Vulkan Windows driver 对透明 Swapchain 存在现实兼容问题，未来可以考虑：

```text
Vulkan offscreen rendering
    ↓
Windows compositor interop
```

但这不是 V1 默认路径。

V1 默认：

```text
Win32 HWND
+
VkSurfaceKHR
+
transparent composite alpha
```

---

# 112. Packaging

输出：

```text
InputHalo.exe
Assets/
Shaders/
licenses/
```

最终尽量：

```text
single folder
```

不要依赖 Visual Studio 安装。

需要的 VC Runtime：

- 静态链接或
- 安装器处理

二选一。

开发阶段无需安装器。

---

# 113. Logs

路径：

```text
%LOCALAPPDATA%\InputHalo\logs\inputhalo.log
```

Rolling：

```text
3 files
5 MB each
```

足够。

---

# 114. Config

路径：

```text
%LOCALAPPDATA%\InputHalo\settings.json
```

Cache：

如字体 atlas 未来持久化，可：

```text
%LOCALAPPDATA%\InputHalo\cache\
```

V1 不需要缓存 atlas。

---

# 115. User Data

InputHalo 不应该产生用户内容文件。

只有：

```text
settings
logs
```

卸载时用户可以安全删除整个：

```text
%LOCALAPPDATA%\InputHalo
```

---

# 116. Accessibility

V1 最低要求：

- Settings 可用 Tab 切换焦点
- Space / Enter 激活 Button / Toggle
- Slider 支持 Arrow
- Esc 关闭 Popup
- Focus state 清晰

不要求完整 Windows UI Automation accessibility tree。

但 UI 架构不要阻止以后添加。

---

# 117. Localization

V1：

```text
English UI
```

源代码所有 UI String 通过 String ID 或集中资源定义。

不要散落：

```cpp
DrawText("Cursor");
```

建议：

```cpp
Strings::Cursor
```

未来可以加中文。

不要在 V1 实现完整 ICU。

---

# 118. Keyboard Layout

显示按键优先表达“操作键”，而不是文本编辑结果。

例如物理快捷键：

```text
Ctrl + S
```

可以通过 Windows key translation 得到合理 label。

V1 不要求完全覆盖世界所有 keyboard layout。

至少测试：

```text
US
Chinese Pinyin environment
```

---

# 119. OBS Compatibility

产品不需要 OBS SDK。

测试：

```text
OBS Display Capture
Windows Screen Capture
```

确保 Overlay 能被常见屏幕捕获方式捕获。

注意：

部分 Window Capture 只捕获目标窗口，不包含 Overlay，这是预期行为。

InputHalo 面向：

```text
Display / Monitor capture
```

不要为 Window Capture 注入目标进程。

---

# 120. Security

禁止：

```text
DLL injection
process injection
driver
kernel service
admin service
```

InputHalo 必须完全用户态。

---

# 121. Performance Profiling

可以接 Tracy。

但 Tracy 不应该是 Release runtime mandatory dependency。

Debug/Dev option：

```cmake
INPUTHALO_ENABLE_TRACY
```

默认：

```text
OFF
```

---

# 122. Compile-time Options

建议：

```text
INPUTHALO_ENABLE_VALIDATION
INPUTHALO_BUILD_TESTS
INPUTHALO_ENABLE_TRACY
```

不要大量 feature flag。

---

# 123. Definition of Done

InputHalo v1.0 的完成定义：

用户可以：

1. 安装/启动。
2. 打开 Cursor Ring。
3. 打开 Keyboard / Mouse Display。
4. 打开 OBS。
5. 录制 Unreal Engine 教程。
6. 视频中能准确看到：
   - 鼠标在哪里
   - 左右键点击
   - 滚轮
   - Ctrl / Shift / Alt 组合
   - 用户需要强调时的 Laser Trail
7. 软件不会挡住 Unreal Engine 的任何鼠标操作。
8. 4K 150% DPI 下仍然清晰。
9. 双屏跨屏正常。
10. 长时间运行稳定。

如果以上十项成立，V1 完成。

不要因为还可以继续加功能而推迟发布。

---

# 124. 推荐默认视觉参数

## Cursor

```text
Style          Ring
Diameter       42 DIP
Border         2 DIP
Opacity        82%
Glow           22%
Accent         #75E6C8
```

## Click Ripple

```text
Start Diameter 22 DIP
End Diameter   54 DIP
Duration       320 ms
Border         2 DIP
```

## Input Visualizer

```text
Anchor         Bottom Center
Bottom Margin  48 DIP
Key Height     34 DIP
Radius         7 DIP
Key Gap        6 DIP
Fade           180 ms
```

## Laser

```text
Mode           Trail
Core Width     5 DIP
Glow Width     14 DIP
Lifetime       1200 ms
Color          #FF5A5F
```

这些是初始值，不是最终美术规范。

---

# 125. 最终技术摘要

```text
PRODUCT
InputHalo

PURPOSE
Input Visualization
Cursor Decoration
Temporary Laser

PLATFORM
Windows 10 / 11 x64

LANGUAGE
C++23

BUILD
CMake 4.4.3

GRAPHICS
Vulkan 1.4.357.0

WINDOWING
Win32

INPUT
Raw Input
GetCursorPos

UI
Custom Retained UI

RENDERING
Custom Vulkan 2D Renderer

TEXT
FreeType + HarfBuzz

VULKAN
Volk + VMA

CONFIG
nlohmann/json

LOGGING
spdlog
```

---

# 126. 核心架构最终图

```text
                    ┌────────────────────┐
                    │     InputHalo      │
                    └─────────┬──────────┘
                              │
                   ┌──────────┴──────────┐
                   │                     │
            Settings Window        Runtime Overlay
                   │                     │
              Custom UI             Feature Layer
                   │            ┌────────┼────────┐
                   │            │        │        │
                   │          Input    Cursor   Laser
                   │            │        │        │
                   └────────────┴────────┴────────┘
                              │
                         Overlay Scene
                              │
                           Painter
                              │
                          Render2D
                              │
                          Vulkan RHI
                              │
                             GPU

                  Windows Platform Services
                 ────────────────────────────
                 Win32 Window / DWM / DPI
                 Raw Input / Monitor / Time
```

---

# 127. 一条最重要的实现原则

> **不要把 InputHalo 做成一个“小型通用引擎”。**
>
> 它只需要成为一个非常漂亮、非常稳定、非常低延迟的 Windows 输入可视化工具。

任何架构决策都应该问：

```text
Does InputHalo v1 actually need this?
```

如果答案不是明确的：

```text
Yes
```

则不实现。

---

# 128. 参考资料

以下资料用于实现阶段核对，不代表要求照抄：

- Vulkan Win32 Surface  
  https://docs.vulkan.org/refpages/latest/refpages/source/vkCreateWin32SurfaceKHR.html

- Vulkan Window System Integration / Composite Alpha  
  https://docs.vulkan.org/spec/latest/chapters/VK_KHR_surface/wsi.html

- CMake Downloads  
  https://cmake.org/download/

- Vulkan SDK  
  https://vulkan.lunarg.com/

---

# 129. Codex 首条任务建议

将此规格书放到：

```text
Docs/InputHalo_Technical_Specification.md
```

然后给 Codex：

```text
Read Docs/InputHalo_Technical_Specification.md completely.

Do not implement the full application yet.

Start only with Milestone 0 and Milestone 1.

Before writing code:
1. Produce a concise implementation plan.
2. List the exact files that will be created.
3. Confirm that no forbidden UI/windowing library will be introduced.
4. Use C++23, CMake 4.4.3, Win32, Vulkan 1.4.357.0.
5. Keep the architecture application-specific rather than building a generic engine.

Then implement, build, run, fix all errors, and report validation results.
```

完成 Milestone 1 后，再执行 Milestone 2：

```text
Read the specification again, especially the Overlay Window sections and Milestone 2.

Implement the smallest possible transparent Vulkan overlay technical spike.

The acceptance test is:
- transparent desktop
- one Vulkan-rendered circle visible
- always-on-top
- click-through
- no black background
- no ImGui, GLFW, SDL, Qt, WinUI, WPF, CEF or WebView
- Vulkan validation clean

Do not proceed to Renderer2D or Settings UI until this spike is verified.
```

这两条任务应该成为项目真正的起点。
