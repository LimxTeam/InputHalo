#include "App/Application.h"
#include "App/PreferenceActions.h"
#include "Platform/Windows/Win32Preferences.h"
#include "ProductInfo.h"
#include "Core/Config/ConfigStore.h"
#include "Core/Logging/Logging.h"
#include "Core/Clock.h"
#include "Platform/Windows/Win32Paths.h"
#include "Platform/Windows/Win32Input.h"
#include "Platform/Windows/Win32Shell.h"
#include "Platform/Windows/Win32Fonts.h"
#include "RHI/Vulkan/VulkanContext.h"
#include "RHI/Vulkan/VulkanSwapchain.h"
#include "Overlay/OverlayManager.h"
#include "UI/SettingsPanel.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <stdexcept>
namespace ih {
namespace {
// Position sampling must keep running even when another process suppresses input messages.
// A per-thread waitable timer avoids changing the system-wide timer resolution.
class InputWakeTimer {
public:
 InputWakeTimer(){m_timer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);}
 ~InputWakeTimer(){if(m_timer)CloseHandle(m_timer);}
 void Wait(DWORD milliseconds){
  LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(milliseconds)*10000;
  if(m_timer&&SetWaitableTimer(m_timer,&due,0,nullptr,nullptr,FALSE)){
   if(MsgWaitForMultipleObjectsEx(1,&m_timer,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)throw std::runtime_error("Input timer wait failed");
  }else if(MsgWaitForMultipleObjectsEx(0,nullptr,milliseconds,QS_ALLINPUT,MWMO_INPUTAVAILABLE)==WAIT_FAILED)throw std::runtime_error("Message wait failed");
 }
private: HANDLE m_timer{};
};
}
int Application::Run(HINSTANCE instance,int showCommand){
 const auto directory=platform::UserDataDirectory();if(!directory)throw std::runtime_error(directory.error());
 Logging logging(*directory/"logs");auto log=spdlog::get("App");log->info("InputHalo {} {} by {} starting",IH_PRODUCT_VERSION,IH_PRODUCT_CHANNEL,IH_TEAM_NAME);
 const auto configPath=*directory/"settings.json";auto config=LoadConfig(configPath);if(!config)throw std::runtime_error(config.error());m_settings=std::move(config->settings);if(!config->warning.empty())spdlog::get("Config")->warn("{}",config->warning);
 if(auto result=m_window.Create(instance);!result)throw std::runtime_error(result.error());
 render::VulkanContext vulkan;if(auto result=vulkan.Initialize(instance,m_window.Handle(),true);!result)throw std::runtime_error(result.error());
 render::VulkanSwapchain swapchain(vulkan);if(auto result=swapchain.Initialize();!result)throw std::runtime_error(result.error());
 bool settingsGraphics=true;
 const auto executable=platform::ExecutableDirectory();if(!executable)throw std::runtime_error(executable.error());const auto shader=*executable/"Shaders/Renderer2D.vert.spv";
 text::TextEngine text;for(const auto& font:platform::DefaultFontPaths())text.AddFont(font);
 ui::SettingsPanel panel(text);panel.ReduceMotion(platform::Win32Input::ReduceMotion());features::OverlayScene scene(text);
 const auto profileDirectory=*directory/"profiles";panel.Library().profiles=ReadProfiles(profileDirectory);
 platform::Win32Input raw;if(auto result=raw.Initialize();!result)throw std::runtime_error(result.error());
 platform::Win32Tray tray;const bool trayReady=tray.Initialize(m_window.Handle());m_window.SetTrayAvailable(trayReady);if(!trayReady)log->warn("Tray icon unavailable; use the taskbar to restore settings");
 tray.Chinese(m_settings.values["general"]["language"]=="zh-CN");
 OverlayManager overlays(vulkan);bool overlayReady=false;
 auto initialize=[&]{auto result=overlays.Initialize(shader);overlayReady=bool(result);if(!result){log->error("Overlay unavailable: {}",result.error());panel.status="Overlay unavailable. See the application log.";}else log->info("Overlay ready on {} monitors",overlays.TargetCount());};initialize();
 m_window.Show(m_settings.values["general"]["start_minimized"].get<bool>()?(trayReady?SW_HIDE:SW_SHOWMINNOACTIVE):showCommand);
 std::string resizeError;
 m_window.SetResizeRender([&]{
  const auto size=m_window.ClientSize();if(size.cx<=0||size.cy<=0)return;
  if(!settingsGraphics){if(auto result=swapchain.Initialize();!result){resizeError=result.error();return;}settingsGraphics=true;}
  const float dpi=GetDpiForWindow(m_window.Handle())/96.f;
  panel.Resize(size.cx/dpi,size.cy/dpi-40,dpi);panel.Chrome(1,m_window.Maximized(),m_window.ChromeHover());
  swapchain.SetContent(shader,panel.Paint(m_settings,Now()),dpi);
  const auto frame=swapchain.Draw(static_cast<uint32_t>(size.cx),static_cast<uint32_t>(size.cy));
  if(!frame)resizeError=frame.error();else if(*frame)m_window.AcknowledgeRedraw();
 });
 struct ResizeCallbackLifetime {platform::Win32Window& window;~ResizeCallbackLifetime(){window.SetResizeRender({});}} resizeLifetime{m_window};
 bool dirty=true,wasActive=false,desktop=true,wasRecording=false,savePending=false,wasInputAvailable=true;
 InputWakeTimer inputWake;
 double nextFrame=0,nextDesktop=0,saveAt=0;render2d::Point lastCursor{-100000,-100000};
 while(!m_window.CloseRequested()){
  m_window.PumpMessages();if(!resizeError.empty())throw std::runtime_error(resizeError);if(m_window.CloseRequested())break;
  const double now=Now();const auto size=m_window.ClientSize();
  if(now>=nextDesktop){desktop=platform::Win32Input::DesktopAvailable();nextDesktop=now+.05;}
  const bool changedDisplay=m_window.ConsumeDisplayChange();if(overlays.ConsumeDisplayChange()||changedDisplay){initialize();dirty=true;scene.Reset();}
  const int action=tray.TakeAction();if(action==3)break;if(action==1){m_window.Show(SW_RESTORE);SetForegroundWindow(m_window.Handle());m_window.RequestRedraw();}if(action==2)panel.ToggleEnabled();
  const bool oldStartup=m_settings.values["general"]["run_at_startup"].get<bool>();
  const float dpi=GetDpiForWindow(m_window.Handle())/96.f;
  const float scale=dpi;
  if(size.cx>0&&size.cy>0)panel.Resize(size.cx/scale,(size.cy-40*dpi)/scale,scale);
  panel.Chrome(dpi/scale,m_window.Maximized(),m_window.ChromeHover());
  auto uiEvents=m_window.TakeEvents();for(auto e:uiEvents){e.y-=40*dpi;panel.Event(e,m_settings,now);}
  auto events=raw.TakeEvents();const bool recording=panel.Recording();
  if(recording!=wasRecording){scene.Reset();dirty=true;}wasRecording=recording;
  const auto cursorSample=platform::Win32Input::TryCursorPosition();
  const bool inputAvailable=desktop&&cursorSample.has_value();
  if(inputAvailable!=wasInputAvailable){
   scene.Reset();events.clear();dirty=true;nextFrame=0;
   // A presentation suspended on the secure desktop must not resume with an expired deadline.
   if(inputAvailable)initialize();
   wasInputAvailable=inputAvailable;
  }
  const bool active=panel.Enabled()&&inputAvailable&&!recording&&!platform::ForegroundExcluded(m_settings);
  if(active!=wasActive){scene.Reset();overlays.SetVisible(active);dirty=true;wasActive=active;}
  for(const auto& e:events){if(recording)panel.Raw(e,m_settings);else if(active){scene.Apply(e,m_settings);dirty=true;}}
  overlays.SetInteractive(active&&scene.DrawingActive(m_settings));
  if(const auto preferenceAction=panel.Library().TakeAction();preferenceAction!=ui::LibraryAction::None){
   scene.Reset();overlays.SetInteractive(false);overlays.SetVisible(false);wasActive=false;dirty=true;
   if(HandlePreferenceAction(m_window.Handle(),profileDirectory,panel.Library(),preferenceAction,m_settings))panel.MarkChanged();
   raw.TakeEvents();panel.Library().Cancel();m_window.TakeEvents();m_window.RequestRedraw();
  }
  if(raw.Failed()){log->error("Raw Input receiver failed");return 1;}
  if(panel.Changed()){
   const bool startup=m_settings.values["general"]["run_at_startup"].get<bool>();
   if(startup!=oldStartup){if(auto result=platform::SetRunAtStartup(startup);!result){m_settings.values["general"]["run_at_startup"]=oldStartup;panel.status="Could not update startup preference";log->error("{}",result.error());}}
   m_settings=SettingsModel::FromJson(m_settings.values);scene.Reset();dirty=true;savePending=true;saveAt=now+.3;tray.Enabled(panel.Enabled());tray.Chinese(m_settings.values["general"]["language"]=="zh-CN");m_window.RequestRedraw();
  }
  if(savePending&&now>=saveAt){auto result=SaveConfig(configPath,m_settings);savePending=false;if(!result){panel.status="Could not save preferences";spdlog::get("Config")->error("{}",result.error());m_window.RequestRedraw();}}
  const auto cursor=cursorSample.value_or(lastCursor);const bool moved=cursor.x!=lastCursor.x||cursor.y!=lastCursor.y;
  const bool animation=scene.Animating(now,m_settings);const bool cursorOn=m_settings.values["cursor"]["enabled"].get<bool>();
  if(moved&&cursorOn)dirty=true;
  if(active&&overlayReady&&now>=nextFrame&&(dirty||animation||(moved&&cursorOn))){
   scene.Update(now,cursor,m_settings);lastCursor=cursor;auto views=overlays.Views();size_t selected=0;
   for(size_t i=0;i<views.size();++i)if(views[i].primary)selected=i;
   {for(size_t i=0;i<views.size();++i){auto b=views[i].bounds;if(cursor.x>=b.x&&cursor.x<b.x+b.width&&cursor.y>=b.y&&cursor.y<b.y+b.height)selected=i;}}
   overlays.SetScenes([&](const OverlayManager::View& view,size_t i){return scene.Paint(view.bounds,view.dpi,i==selected,now,m_settings);});dirty=false;nextFrame=now+.016;
  }
  bool retry=false;if(active&&overlayReady){auto result=overlays.Update();if(!result){log->error("Overlay disabled: {}",result.error());overlayReady=false;panel.status="Overlay unavailable. See the application log.";m_window.RequestRedraw();}else retry=*result;}
  if(vulkan.DeviceLost()){log->error("Graphics device lost; exiting");return 1;}
  if((size.cx<=0||size.cy<=0)&&settingsGraphics){swapchain.Shutdown();settingsGraphics=false;}
  if(size.cx>0&&size.cy>0&&(m_window.NeedsRedraw()||!uiEvents.empty()||recording||panel.Animating(now))){
   if(!settingsGraphics){if(auto result=swapchain.Initialize();!result)throw std::runtime_error(result.error());settingsGraphics=true;}
   swapchain.SetContent(shader,panel.Paint(m_settings,now),scale);auto result=swapchain.Draw(static_cast<uint32_t>(size.cx),static_cast<uint32_t>(size.cy));if(!result)throw std::runtime_error(result.error());if(*result)m_window.AcknowledgeRedraw();else retry=true;
  }
  DWORD wait=250;if(retry||panel.Animating(now)||recording)wait=16;
  // Poll cursor position independently of WM_INPUT, including over elevated windows.
  if(active&&overlayReady&&cursorOn)wait=std::min(wait,DWORD{16});
  if(!inputAvailable)wait=std::min(wait,DWORD{50});
  if(active&&(dirty||scene.Animating(now,m_settings)))wait=static_cast<DWORD>(std::clamp((nextFrame-Now())*1000.,1.,16.));
  if(retry)wait=std::min(wait,DWORD{1});
  if(savePending)wait=std::min(wait,static_cast<DWORD>(std::clamp((saveAt-Now())*1000.,1.,300.)));
  inputWake.Wait(wait);
 }
 m_window.SetResizeRender({});scene.Reset();overlays.Shutdown();swapchain.Shutdown();vulkan.Shutdown();
 if(auto result=SaveConfig(configPath,m_settings);!result){spdlog::get("Config")->error("{}",result.error());return 1;}
 log->info("Clean shutdown");return vulkan.ValidationErrors()?1:0;
}
}

