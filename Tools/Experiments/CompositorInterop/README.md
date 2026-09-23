# Vulkan → DirectComposition 透明 Overlay 实验

**本机技术验证通过；尚未接入 InputHalo 主程序。**

在直接 Win32 Vulkan swapchain 仅支持 OPAQUE 的机器上，按规格第 111 节提出的互操作方向进行了独立实验。
这会改变最终产品的呈现方式，因此保持在 `Tools/Experiments`；未修改主应用或规格书的默认路径。

## 渲染路径

```text
Vulkan compute shader 绘制预乘 RGBA 圆
    ↓ VK_KHR_external_memory_win32 导入同一块共享纹理
D3D11 CopyResource（仅 GPU 复制，无 D3D 绘制）
    ↓ DXGI flip sequential / PREMULTIPLIED
DirectComposition visual → 每屏一个 HWND → 桌面
```

Vulkan 和 DXGI 按 adapter LUID 匹配。`VK_KHR_win32_keyed_mutex` 使用 key 0/1 交接共享纹理，
配合 Synchronization2 外部队列所有权屏障。等待 Vulkan fence 后进行 D3D11 复制，再释放 mutex。
这是验证正确性优先的串行实验，尚未优化为产品所需的低延迟双缓冲。

没有使用 Qt、GLFW、SDL、ImGui、WinUI、WebView 等框架。圆的像素由 Vulkan 生成。
呈现过程不经过 CPU；CPU staging readback 和三个桌面像素采样仅用于诊断，均只在测试初期执行。
没有添加屏幕捕获产品功能。

## 构建与复测

```powershell
cmake --preset windows-msvc
cmake --build --preset debug --target InputHaloInteropSpike InputHaloInteropBackdrop
.\Tools\Test-Interop.ps1

cmake --build --preset release --target InputHaloInteropSpike InputHaloInteropBackdrop
.\Tools\Test-Interop.ps1 -Configuration Release
```

依赖现有 Vulkan SDK 中的 DXC，将 HLSL 编译为 SPIR-V，输出 `InteropCircle.spv` 到可执行文件旁。
该 shader 必须和实验 EXE 一起保留。实验目标均为 `EXCLUDE_FROM_ALL`，不会进入正常产品构建。

默认模式每屏绘制 121 帧后自动退出，最长 30 秒；`-Preview` 显示 60 秒后退出。
日志和报告位于 `Build/windows-msvc/<Debug|Release>/interop-results/`。
启动时报告先写为 RUNNING，失败时写入错误，避免误读上次成功报告。

脚本为本次进程隔离旧 OBS hook，并在 Debug 下使用同步验证；不修改系统设置。

## 视觉与跨进程点击检查

1. 启动 `Build/windows-msvc/Debug/InputHaloInteropBackdrop.exe`。
2. 在另一个终端运行 `Tools/Test-Interop.ps1 -Preview`。
3. 确认测试窗口上可见薄荷绿色圆，圆外仍是原来的窗口内容，没有黑色全屏背景。
4. 点击圆心，独立测试窗口标题 `clicks: 0` 应变为 `clicks: 1`。
5. 测试窗口按 **F2** 移动到第二块屏幕，重复检查和点击。
6. 关闭测试窗口；Overlay 最迟在 60 秒后自动结束。

测试窗口客户区同样由 Vulkan 清屏，Win32 只用于窗口和鼠标消息计数。
Overlay 使用 TOPMOST、TOOLWINDOW、NOACTIVATE、TRANSPARENT、LAYERED、NOREDIRECTIONBITMAP。
LAYERED 的整体 alpha 保持 255；每像素透明由 composition swapchain 的预乘 alpha 提供，不使用 Color Key。

## 已验证的证据

2026-09-23，Windows 11，NVIDIA RTX 3060，两个 1920×1080 / 96 DPI 显示器：

- Vulkan/DXGI LUID 匹配，共享 RGBA8 storage image 导入成功。
- 每屏共享纹理读回：圆外 `[0,0,0,0]`；圆心 `[99,195,170,217]`，符合 85% 预乘 alpha。
- 两屏均看到圆覆盖在独立测试窗口上，周围无黑色背景。
- 两屏分别实际点击圆心，跨进程测试窗口点击计数均从 0 变为 1。
- 展示 Overlay 没有抢占前台窗口。
- 一轮 Debug 60 秒预览完成每屏 3171 帧，Vulkan 同步验证 0 错误、0 警告。
- 最终 Debug/Release 均构建并运行成功，每屏各 121 帧；两种配置的单元测试均通过（2/2）。Release 按设计关闭验证层。
- 桌面像素采样也显示圆心发生变化，圆外左右各 100 px 的背景保持原值。

机器生成的 JSON 只标记 GPU/像素结果，视觉和跨进程点击结果由本节记录，不伪装为自动测试结论。
归档报告：`Results/RTX3060-Debug.json`、`Results/RTX3060-Release.json` 和 `Results/RTX3060-Debug-preview.json`。

## 仍未验收

- AMD、Intel、跨 GPU 显示器、混合 DPI、HDR、多种刷新率。
- 热插拔、休眠恢复、长时间稳定性、OBS Display Capture。
- 性能目标、内存预算、双缓冲调度和正式产品错误 UI。

显示器布局变更时实验退出，不重建；固定圆不跟随鼠标。这些是实验边界，不是产品行为。
下一步应将已验证路径整理成最小平台呈现模块，再接入主程序；不要直接把实验的单帧同步循环当成最终架构。

## 参考

- [Vulkan Win32 keyed mutex](https://docs.vulkan.org/refpages/latest/refpages/source/VkWin32KeyedMutexAcquireReleaseInfoKHR.html)
- [Vulkan 外部内存句柄属性](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetMemoryWin32HandlePropertiesKHR.html)
- [Microsoft composition swapchain](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)
- [Microsoft 共享纹理和 keyed mutex](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_resource_misc_flag)
