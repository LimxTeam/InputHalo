/*******************************************************************************
 * 文件: Application.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   InputHalo 应用程序主实现文件，负责编排整个应用的完整生命周期。
 *   包括：初始化日志系统、加载用户配置、创建 Win32 窗口、初始化 Vulkan
 *   渲染上下文与交换链、加载字体、构建 UI 设置面板、初始化原始输入接收器与
 *   系统托盘图标、初始化多显示器覆盖层管理器，然后进入主循环。
 *   主循环处理窗口消息泵、输入事件分发、覆盖场景更新与渲染、配置延迟持久化，
 *   并在退出时逆序释放所有子系统资源。
 *   算法复杂度：主循环每帧 O(N)，N 为输入事件数与显示器数量；无复杂排序算法。
 *   空间-时间权衡：缓存最近光标位置与动画状态以避免每帧重复查询，
 *   以极少量内存换取渲染触发判断的快速响应。
 *
 * 设计哲学:
 *   采用显式生命周期管理：所有子系统在 Run() 中按需顺序初始化，退出时逆序释放。
 *   主循环通过 dirty 标志 + 动画状态 + 光标移动三条件判断是否触发重绘，
 *   实现按需渲染而非固定帧率，显著降低空闲时 CPU/GPU 占用。
 *   配置变更采用延迟写入（savePending + saveAt 时间阈值），避免频繁磁盘 I/O。
 *   对显示器热插拔、安全桌面切换、输入可用性变化等边界情况做了防御性重置。
 *
 * 技术特性:
 *   - 高分辨率等待定时器: 使用 CreateWaitableTimerExW 创建每线程定时器，
 *     配合 MsgWaitForMultipleObjectsEx 在阻塞等待窗口消息的同时保持精确帧节拍，
 *     且不改变系统全局定时器分辨率（避免影响其他进程）。
 *   - 按需渲染: dirty/animation/moved 三条件决定是否绘制覆盖层。
 *   - 多显示器支持: OverlayManager 管理多个显示器上的独立覆盖层视图。
 *   - 系统托盘集成: 托盘图标支持显示窗口、暂停覆盖等快捷操作。
 *   - DPI 感知: 每窗口 DPI 缩放，适配高分辨率显示器。
 *   - 延迟配置保存: 配置修改后延迟 300ms 写入磁盘，合并连续修改。
 *
 * 依赖关系:
 *   内部: ih::Core (Clock, Logging, ConfigStore)、ih::Platform (Win32Window,
 *         Win32Input, Win32Tray, Win32Paths, Win32Fonts, Win32Preferences)、
 *         ih::Render (VulkanContext, VulkanSwapchain)、ih::Overlay (OverlayManager)、
 *         ih::UI (SettingsPanel)、ih::Features (OverlayScene)
 *   外部: Win32 API (CreateWaitableTimerExW, MsgWaitForMultipleObjectsEx,
 *         GetDpiForWindow, SetForegroundWindow 等)、spdlog 日志库
 *
 * 注意事项:
 *   - Windows x64 平台专用，深度依赖 Win32 API。
 *   - 主循环中对设备丢失、原始输入接收失败等错误采取立即退出策略。
 *   - 覆盖层在安全桌面（UAC 提示屏）期间自动暂停，恢复时重新初始化。
 *
 ******************************************************************************/
// 应用层核心头文件：定义 Application 主类
#include "App/Application.h"
// 偏好设置动作处理：配置文件导入/导出/删除等
#include "App/PreferenceActions.h"
// Win32 平台偏好设置：开机自启、文件选择对话框等
#include "Platform/Windows/Win32Preferences.h"
// 产品信息宏：版本号、团队名等（由 CMake 模板生成）
#include "ProductInfo.h"
// 配置存储：JSON 配置文件的加载与保存
#include "Core/Config/ConfigStore.h"
// 日志系统：spdlog 封装的旋转文件日志
#include "Core/Logging/Logging.h"
// 高精度时钟：steady_clock 封装，提供秒级时间戳
#include "Core/Clock.h"
// Win32 路径工具：用户数据目录、可执行文件目录等
#include "Platform/Windows/Win32Paths.h"
// Win32 原始输入：Raw Input API 封装，捕获全局键鼠事件
#include "Platform/Windows/Win32Input.h"
// Win32 Shell：开机自启注册等外壳操作
#include "Platform/Windows/Win32Shell.h"
// Win32 字体：系统默认字体路径获取
#include "Platform/Windows/Win32Fonts.h"
// Vulkan 渲染上下文：实例、设备、队列初始化
#include "RHI/Vulkan/VulkanContext.h"
// Vulkan 交换链：双缓冲交换链，呈现 UI 设置面板画面
#include "RHI/Vulkan/VulkanSwapchain.h"
// 覆盖层管理器：管理多显示器透明覆盖窗口的渲染
#include "Overlay/OverlayManager.h"
// UI 设置面板：设置界面的绘制与事件处理
#include "UI/SettingsPanel.h"
// spdlog 第三方日志库
#include <spdlog/spdlog.h>
// 标准算法库：std::min/std::clamp 等
#include <algorithm>
// 标准异常：std::runtime_error 用于错误传播
#include <stdexcept>
// InputHalo 项目根命名空间
namespace ih {
// 匿名命名空间：以下内容仅在本编译单元可见
namespace {
// InputWakeTimer 说明：位置采样必须在其他进程抑制输入消息时仍保持运行，
// 使用每线程高分辨率可等待定时器，避免改变系统全局定时器分辨率。
// Position sampling must keep running even when another process suppresses input messages.
// A per-thread waitable timer avoids changing the system-wide timer resolution.
// InputWakeTimer 类：高分辨率等待定时器，在等待窗口消息时维持精确帧节拍
class InputWakeTimer {
// --- 公共接口 ---
public:
// 构造：创建高分辨率可等待定时器，请求修改状态与同步访问权限
 InputWakeTimer(){m_timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);}
// 析构：关闭定时器句柄，释放内核对象
 ~InputWakeTimer(){if(m_timer)CloseHandle(m_timer);}
// Wait — 等待指定毫秒数，同时响应窗口输入消息；
// 优先使用高分辨率定时器，失败则退化为纯消息等待
 void Wait(DWORD milliseconds){
// 设置定时器到期时间（负值表示相对时间，单位 100ns）
  LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(milliseconds)*10000;
// 高分辨率定时器路径：设置定时器后等待其触发或输入消息到达
  if(m_timer&&SetWaitableTimer(m_timer,&due,0,nullptr,nullptr,FALSE)){
// 等待定时器对象或任何输入消息；MWMO_INPUTAVAILABLE 确保已排队消息也被唤醒
   if(MsgWaitForMultipleObjectsEx(1,&m_timer,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)throw std::runtime_error("Input timer wait failed");
// 降级路径：定时器不可用时，仅等待窗口消息（带超时）
  }else if(MsgWaitForMultipleObjectsEx(0,nullptr,milliseconds,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)throw std::runtime_error("Message wait failed");
 }
// --- 私有成员 ---
private: HANDLE m_timer{};
// InputWakeTimer 类定义结束
};
// 匿名命名空间结束
}
// ========================================================================
// Application::Run — 应用程序主入口与主循环
// 参数: instance — 程序实例句柄; showCommand — 初始窗口显示方式
// 返回: 进程退出码（0 成功，非 0 错误）
// ========================================================================
int Application::Run(HINSTANCE instance,int showCommand){
// 第一步：获取用户数据目录（存放日志、配置文件、配置预设）
 const auto directory=platform::UserDataDirectory();if(!directory)throw std::runtime_error(directory.error());
// 第二步：初始化日志系统（旋转文件日志），记录启动信息
 Logging logging(*directory/"logs");auto log=spdlog::get("App");log->info("InputHalo {} {} by {} starting",IH_PRODUCT_VERSION,IH_PRODUCT_CHANNEL,IH_TEAM_NAME);
// 第三步：加载用户配置文件；若配置损坏则自动备份并回退到默认值
 const auto configPath=*directory/"settings.json";auto config=LoadConfig(configPath);if(!config)throw std::runtime_error(config.error());m_settings=std::move(config->settings);if(!config->warning.empty())spdlog::get("Config")->warn("{}",config->warning);
// 第四步：创建主窗口（Win32 窗口）
 if(auto result=m_window.Create(instance);!result)throw std::runtime_error(result.error());
// 第五步：初始化 Vulkan 渲染上下文（实例、物理设备、逻辑设备、队列）
 render::VulkanContext vulkan;if(auto result=vulkan.Initialize(instance,m_window.Handle(),true);!result)throw std::runtime_error(result.error());
// 第六步：初始化交换链，用于呈现设置面板 UI
 render::VulkanSwapchain swapchain(vulkan);if(auto result=swapchain.Initialize();!result)throw std::runtime_error(result.error());
// 跟踪交换链是否已初始化（窗口最小化时可能销毁以节省资源）
 bool settingsGraphics=true;
// 获取可执行文件目录，定位预编译的 Vulkan shader（SPIR-V 格式）
 const auto executable=platform::ExecutableDirectory();if(!executable)throw std::runtime_error(executable.error());const auto shader=*executable/"Shaders/Renderer2D.vert.spv";
// 初始化文本引擎，加载系统默认字体（用于 UI 文字渲染）
 text::TextEngine text;for(const auto& font:platform::DefaultFontPaths())text.AddFont(font);
// 创建设置面板 UI，读取系统"减少动画"偏好；创建覆盖场景
 ui::SettingsPanel panel(text);panel.ReduceMotion(platform::Win32Input::ReduceMotion());features::OverlayScene scene(text);
// 加载配置预设库（用户保存的设置模板）
 const auto profileDirectory=*directory/"profiles";panel.Library().profiles=ReadProfiles(profileDirectory);
// 初始化原始输入接收器（Raw Input），捕获全局键鼠事件
 platform::Win32Input raw;if(auto result=raw.Initialize();!result)throw std::runtime_error(result.error());
// 初始化系统托盘图标；若失败则记录警告并允许通过任务栏恢复设置窗口
 platform::Win32Tray tray;const bool trayReady=tray.Initialize(m_window.Handle());m_window.SetTrayAvailable(trayReady);if(!trayReady)log->warn("Tray icon unavailable; use the taskbar to restore settings");
// 根据当前语言设置托盘菜单的显示语言
 tray.Chinese(m_settings.values["general"]["language"]=="zh-CN");
// 创建覆盖层管理器（多显示器透明覆盖窗口）
 OverlayManager overlays(vulkan);bool overlayReady=false;
// 覆盖层初始化 lambda：加载 shader 并初始化所有显示器覆盖层；
// 失败时记录错误并在 UI 面板状态中提示。此 lambda 在显示器热插拔时也会被调用。
 auto initialize=[&]{auto result=overlays.Initialize(shader);overlayReady=bool(result);if(!result){log->error("Overlay unavailable: {}",result.error());panel.status="Overlay unavailable. See the application log.";}else log->info("Overlay ready on {} monitors",overlays.TargetCount());};initialize();
// 根据"启动时最小化"设置决定窗口初始显示方式
 m_window.Show(m_settings.values["general"]["start_minimized"].get<bool>()?(trayReady?SW_HIDE:SW_SHOWMINNOACTIVE):showCommand);
// 存储窗口大小调整时的渲染错误（延迟到主循环抛出）
 std::string resizeError;
// 设置窗口大小变化时的渲染回调：重新初始化交换链、调整 UI 布局并绘制一帧
 m_window.SetResizeRender([&]{
// 获取客户区尺寸，为零时跳过渲染
  const auto size=m_window.ClientSize();if(size.cx<=0||size.cy<=0)return;
// 若交换链已被销毁（窗口曾最小化），在此恢复初始化
  if(!settingsGraphics){if(auto result=swapchain.Initialize();!result){resizeError=result.error();return;}settingsGraphics=true;}
// 获取当前窗口 DPI 缩放比
  const float dpi=GetDpiForWindow(m_window.Handle())/96.f;
// 调整 UI 面板尺寸与窗口边框样式（标题栏高度 40px）
  panel.Resize(size.cx/dpi,size.cy/dpi-40,dpi);panel.Chrome(1,m_window.Maximized(),m_window.ChromeHover());
// 将 UI 面板绘制内容提交到交换链
  swapchain.SetContent(shader,panel.Paint(m_settings,Now()),dpi);
// 绘制一帧并呈现
  const auto frame=swapchain.Draw(static_cast<uint32_t>(size.cx),static_cast<uint32_t>(size.cy));
// 处理绘制结果：失败记录错误，成功则确认重绘（消除 Win32 重绘请求）
  if(!frame)resizeError=frame.error();else if(*frame)m_window.AcknowledgeRedraw();
// 大小调整回调结束
 });
// RAII 守卫：析构时自动清除窗口的 resize 渲染回调，防止悬空引用
 struct ResizeCallbackLifetime {platform::Win32Window& window;~ResizeCallbackLifetime(){window.SetResizeRender({});}} resizeLifetime{m_window};
// 主循环状态变量：
// dirty          — 覆盖层内容是否需要重绘
// wasActive      — 上一帧覆盖层是否处于激活状态
// desktop        — 桌面是否可用（非安全桌面/UAC 屏幕）
// wasRecording   — 上一帧是否处于录制模式
// savePending    — 是否有待保存的配置
// wasInputAvailable — 上一帧输入是否可用
 bool dirty=true,wasActive=false,desktop=true,wasRecording=false,savePending=false,wasInputAvailable=true;
// 创建高分辨率等待定时器实例
 InputWakeTimer inputWake;
// 帧调度时间戳：nextFrame 下一帧渲染时间，nextDesktop 下次桌面检测时间，
// saveAt 配置保存截止时间；lastCursor 记录上次光标位置以检测移动
 double nextFrame=0,nextDesktop=0,saveAt=0;render2d::Point lastCursor{-100000,-100000};
// ==================== 主循环开始 ====================
// 直到用户请求关闭窗口为止
 while(!m_window.CloseRequested()){
// 泵送 Windows 消息队列；若 resize 回调中记录了错误则抛出
  m_window.PumpMessages();if(!resizeError.empty())throw std::runtime_error(resizeError);if(m_window.CloseRequested())break;
// 获取当前高精度时间戳与窗口客户区尺寸
  const double now=Now();const auto size=m_window.ClientSize();
// 每 50ms 检测一次桌面是否可用（安全桌面检测节流）
  if(now>=nextDesktop){desktop=platform::Win32Input::DesktopAvailable();nextDesktop=now+.05;}
// 检测显示器配置变化（分辨率/插拔），变化时重新初始化覆盖层并重置场景
  const bool changedDisplay=m_window.ConsumeDisplayChange();if(overlays.ConsumeDisplayChange()||changedDisplay){initialize();dirty=true;scene.Reset();}
// 处理托盘图标动作：3=退出，1=显示窗口，2=切换覆盖层开关
  const int action=tray.TakeAction();if(action==3)break;if(action==1){m_window.Show(SW_RESTORE);SetForegroundWindow(m_window.Handle());m_window.RequestRedraw();}if(action==2)panel.ToggleEnabled();
// 记录当前开机自启设置值（用于后续变更检测）
  const bool oldStartup=m_settings.values["general"]["run_at_startup"].get<bool>();
// 获取窗口 DPI
  const float dpi=GetDpiForWindow(m_window.Handle())/96.f;
// 缩放系数等于 DPI
  const float scale=dpi;
// 窗口有有效尺寸时调整 UI 面板布局（扣除标题栏高度）
  if(size.cx>0&&size.cy>0)panel.Resize(size.cx/scale,(size.cy-40*dpi)/scale,scale);
// 更新面板边框样式（最大化状态、悬停状态）
  panel.Chrome(dpi/scale,m_window.Maximized(),m_window.ChromeHover());
// 取出窗口 UI 事件，转换为面板坐标系后分发给设置面板
  auto uiEvents=m_window.TakeEvents();for(auto e:uiEvents){e.y-=40*dpi;panel.Event(e,m_settings,now);}
// 取出原始输入事件；获取录制状态
  auto events=raw.TakeEvents();const bool recording=panel.Recording();
// 录制模式变化时重置覆盖场景
  if(recording!=wasRecording){scene.Reset();dirty=true;}wasRecording=recording;
// 独立轮询光标位置（包括在提权窗口上也能获取），不依赖 WM_INPUT 消息
  const auto cursorSample=platform::Win32Input::TryCursorPosition();
// 判断输入是否可用：桌面可用且光标位置可获取
  const bool inputAvailable=desktop&&cursorSample.has_value();
// 输入可用性变化（如进入/离开安全桌面）时：重置场景与事件队列，
  if(inputAvailable!=wasInputAvailable){
// 清空事件并重置帧调度；挂起的呈现不能用过时期限恢复
// 若恢复输入可用则重新初始化覆盖层
   scene.Reset();events.clear();dirty=true;nextFrame=0;
   // A presentation suspended on the secure desktop must not resume with an expired deadline.
   if(inputAvailable)initialize();
   wasInputAvailable=inputAvailable;
// 计算覆盖层是否应处于激活状态：
// 面板已启用 && 输入可用 && 非录制模式 && 前台进程未被排除
  }
// 激活状态变化时：重置场景、设置覆盖层可见性、标记需要重绘
  const bool active=panel.Enabled()&&inputAvailable&&!recording&&!platform::ForegroundExcluded(m_settings);
// 分发输入事件：录制模式下交给面板记录，激活状态下交给覆盖场景处理
  if(active!=wasActive){scene.Reset();overlays.SetVisible(active);dirty=true;wasActive=active;}
// 根据场景是否处于绘图模式，决定覆盖层是否可交互
  for(const auto& e:events){if(recording)panel.Raw(e,m_settings);else if(active){scene.Apply(e,m_settings);dirty=true;}}
// 处理偏好设置动作（应用/删除/保存/导入/导出/添加排除应用等）
  overlays.SetInteractive(active&&scene.DrawingActive(m_settings));
// 执行动作前重置场景与覆盖层状态
  if(const auto preferenceAction=panel.Library().TakeAction();preferenceAction!=ui::LibraryAction::None){
// 调用偏好动作处理器；成功则标记面板已修改
   scene.Reset();overlays.SetInteractive(false);overlays.SetVisible(false);wasActive=false;dirty=true;
// 动作完成后清空事件队列并请求重绘
   if(HandlePreferenceAction(m_window.Handle(),profileDirectory,panel.Library(),preferenceAction,m_settings))panel.MarkChanged();
   raw.TakeEvents();panel.Library().Cancel();m_window.TakeEvents();m_window.RequestRedraw();
// 原始输入接收器失败（如权限丢失），记录错误并退出
  }
// 设置面板内容发生变化时处理
  if(raw.Failed()){log->error("Raw Input receiver failed");return 1;}
// 检查开机自启设置是否变化
  if(panel.Changed()){
// 开机自启变更：失败时回滚设置值并提示用户
   const bool startup=m_settings.values["general"]["run_at_startup"].get<bool>();
// 重新构建设置模型（触发校验），重置场景，标记需要保存（延迟 300ms），
// 同步托盘状态与语言，请求重绘
   if(startup!=oldStartup){if(auto result=platform::SetRunAtStartup(startup);!result){m_settings.values["general"]["run_at_startup"]=oldStartup;panel.status="Could not update startup preference";log->error("{}",result.error());}}
   m_settings=SettingsModel::FromJson(m_settings.values);scene.Reset();dirty=true;savePending=true;saveAt=now+.3;tray.Enabled(panel.Enabled());tray.Chinese(m_settings.values["general"]["language"]=="zh-CN");m_window.RequestRedraw();
// 延迟保存配置：到达保存时间点时写入磁盘；失败时在面板状态栏提示
  }
// 获取当前光标位置，检测是否相对于上一帧发生了移动
  if(savePending&&now>=saveAt){auto result=SaveConfig(configPath,m_settings);savePending=false;if(!result){panel.status="Could not save preferences";spdlog::get("Config")->error("{}",result.error());m_window.RequestRedraw();}}
// 判断场景是否正在播放动画，以及光标指示是否启用
  const auto cursor=cursorSample.value_or(lastCursor);const bool moved=cursor.x!=lastCursor.x||cursor.y!=lastCursor.y;
// 光标移动且光标指示启用时，标记需要重绘
  const bool animation=scene.Animating(now,m_settings);const bool cursorOn=m_settings.values["cursor"]["enabled"].get<bool>();
// ===== 覆盖层渲染逻辑 =====
// 满足条件时绘制一帧覆盖层：激活 && 就绪 && 到达帧时间 &&（脏/动画/光标移动）
  if(moved&&cursorOn)dirty=true;
// 更新场景状态，获取所有显示器视图
  if(active&&overlayReady&&now>=nextFrame&&(dirty||animation||(moved&&cursorOn))){
// 默认选中主显示器视图
   scene.Update(now,cursor,m_settings);lastCursor=cursor;auto views=overlays.Views();size_t selected=0;
// 若光标位于某个显示器的边界内，则选中该显示器视图（交互区域跟随光标）
   for(size_t i=0;i<views.size();++i)if(views[i].primary)selected=i;
// 为每个显示器视图调用场景绘制函数，设置渲染场景；重置 dirty，调度下一帧
   {for(size_t i=0;i<views.size();++i){auto b=views[i].bounds;if(cursor.x>=b.x&&cursor.x<b.x+b.width&&cursor.y>=b.y&&cursor.y<b.y+b.height)selected=i;}}
   overlays.SetScenes([&](const OverlayManager::View& view,size_t i){return scene.Paint(view.bounds,view.dpi,i==selected,now,m_settings);});dirty=false;nextFrame=now+.016;
// 更新覆盖层呈现；失败时禁用覆盖层并提示用户，retry 标志表示需重试
  }
// Vulkan 设备丢失（如显卡驱动崩溃），记录错误并退出
  bool retry=false;if(active&&overlayReady){auto result=overlays.Update();if(!result){log->error("Overlay disabled: {}",result.error());overlayReady=false;panel.status="Overlay unavailable. See the application log.";m_window.RequestRedraw();}else retry=*result;}
// 窗口最小化（客户区为零）时销毁交换链以节省 GPU 资源
  if(vulkan.DeviceLost()){log->error("Graphics device lost; exiting");return 1;}
// ===== 设置面板 UI 渲染 =====
// 窗口有有效尺寸且需要重绘时绘制面板
  if((size.cx<=0||size.cy<=0)&&settingsGraphics){swapchain.Shutdown();settingsGraphics=false;}
// 交换链未初始化时恢复初始化
  if(size.cx>0&&size.cy>0&&(m_window.NeedsRedraw()||!uiEvents.empty()||recording||panel.Animating(now))){
// 绘制设置面板 UI 并呈现；失败时抛出异常或设置重试标志
   if(!settingsGraphics){if(auto result=swapchain.Initialize();!result)throw std::runtime_error(result.error());settingsGraphics=true;}
   swapchain.SetContent(shader,panel.Paint(m_settings,now),scale);auto result=swapchain.Draw(static_cast<uint32_t>(size.cx),static_cast<uint32_t>(size.cy));if(!result)throw std::runtime_error(result.error());if(*result)m_window.AcknowledgeRedraw();else retry=true;
// ===== 计算本帧空闲等待时间 =====
// 默认等待 250ms；有重绘重试或动画或录制时缩短到 16ms
  }
// 光标位置独立轮询（不依赖 WM_INPUT），包括在提权窗口上也能获取
  DWORD wait=250;if(retry||panel.Animating(now)||recording)wait=16;
// 覆盖层激活且光标指示启用时，以 16ms 节拍轮询光标
  // Poll cursor position independently of WM_INPUT, including over elevated windows.
// 输入不可用时降低轮询频率到 50ms，减少空转
  if(active&&overlayReady&&cursorOn)wait=std::min(wait,DWORD{16});
// 有待渲染内容时，精确等待到下一帧时间点（1~16ms）
  if(!inputAvailable)wait=std::min(wait,DWORD{50});
// 呈现重试时以最短间隔等待（1ms）
  if(active&&(dirty||scene.Animating(now,m_settings)))wait=static_cast<DWORD>(std::clamp((nextFrame-Now())*1000.,1.,16.));
// 有待保存配置时，等待到保存时间点（1~300ms）
  if(retry)wait=std::min(wait,DWORD{1});
// 高分辨率等待：阻塞直到窗口消息到达或定时器到期
  if(savePending)wait=std::min(wait,static_cast<DWORD>(std::clamp((saveAt-Now())*1000.,1.,300.)));
  inputWake.Wait(wait);
// ==================== 主循环结束 ====================
 }
// ===== 清理与退出 =====
// 清除渲染回调，重置场景，逆序关闭覆盖层、交换链、Vulkan 设备
 m_window.SetResizeRender({});scene.Reset();overlays.Shutdown();swapchain.Shutdown();vulkan.Shutdown();
// 退出前保存最终配置；失败时记录错误码
 if(auto result=SaveConfig(configPath,m_settings);!result){spdlog::get("Config")->error("{}",result.error());return 1;}
// 记录干净退出日志；若 Vulkan 验证层有错误则返回非零退出码
 log->info("Clean shutdown");return vulkan.ValidationErrors()?1:0;
// Application::Run 方法结束
}
}

