# Milestone 2 — 透明 Overlay 技术验证

**状态：能力探测已完成，透明 Overlay 验收未通过。**

2026-09-23 在本机 Windows 11 / NVIDIA RTX 3060（Vulkan API 1.4.325）实测：

- 两个显示器均为 1920×1080、96 DPI，桌面原点分别为 `(0, 0)` 和 `(9, 1080)`。
- 一个 Vulkan 物理设备；每屏分别创建具有目标大小和位置的隐藏 Overlay HWND 与 VkSurfaceKHR。
- 六种组合：原始 Popup、DWM Extend Frame、DWM Blur Region、两种 DWM 方法叠加、Layered + Blur、NoRedirectionBitmap + Blur。
- DWM composition 已开启，所有 DWM 调用返回 `S_OK`。
- 12 个 surface 配置的 `supportedCompositeAlpha` 均为 **1 / OPAQUE**。
- 无 PRE_MULTIPLIED、POST_MULTIPLIED 或 INHERIT 可选；Debug 验证层 **0 错误、0 警告**。
- Release 对照探测得到相同能力结果；Debug/Release 的 Config 与 OverlayAlphaPolicy 测试均通过（2/2）。

原始报告见 [Results/RTX3060-2026-09-23.json](Results/RTX3060-2026-09-23.json)。

## 复现

在仓库根目录运行：

```powershell
cmake --preset windows-msvc
cmake --build --preset debug --target InputHaloOverlayProbe
.\Tools\Probe-Overlay.ps1 -Configuration Debug

# Release 对照
cmake --build --preset release --target InputHaloOverlayProbe
.\Tools\Probe-Overlay.ps1 -Configuration Release
```

新报告写入 `Build/windows-msvc/overlay-probe-results/report.json`，日志写入同目录 `inputhalo.log`。
脚本仅对子进程隔离旧 OBS 注入层并开启同步验证，退出时恢复环境变量。

退出码：`0` = 找到待渲染验证的候选模式，`2` = 无透明候选，`1` = 执行或验证层错误。
**退出码 0 也不等于透明视觉验收通过。**

## 判断范围

本实验是 swapchain 创建前的能力门槛。没有创建交换链，没有绘制圆，没有显示覆盖桌面的窗口，
也没有完成置顶可见性、跨进程点击穿透或无黑色背景的视觉验收。
不使用 GDI Color Key，不强行请求驱动未声明支持的 alpha 位，不将 OPAQUE 当作透明 fallback。
`INHERIT` 即使将来出现，也仍需检查 native alpha 配置及实际像素。

这只能证明**本机当前驱动和这些窗口配置不满足规格指定路径的前置条件**，不代表所有 Windows/GPU 都不支持。
Layered / NoRedirectionBitmap 只是比较项，不作为已验证的产品策略。

按规格第 109–111 节，实验未通过前不并入产品渲染路径，不进入 Renderer2D/Settings UI。
主应用保留已验收的 Milestone 1 设置窗口；探测程序输出 `OVERLAY_COMPATIBILITY_ERROR`。
尚未实现产品内兼容性错误 UI，因为透明方案仍处于独立实验阶段。

初始探测后的两个方向是：在其他驱动/GPU 上复测直接交换链路径，或按第 111 节的互操作方向，
单独研究 Vulkan 离屏 RGBA → Windows compositor interop。

后续进展：已完成独立的 [DirectComposition 互操作实验](../CompositorInterop/README.md)，
本机透明圆及双屏点击穿透验证通过。直接 Vulkan swapchain 的上述能力结论保持不变。

## 依据

- [Vulkan composite alpha 定义](https://docs.vulkan.org/refpages/latest/refpages/source/VkCompositeAlphaFlagBitsKHR.html)：OPAQUE 忽略图像 alpha；INHERIT 需要应用配置 native blending。
- [Microsoft DwmEnableBlurBehindWindow](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmenableblurbehindwindow)
- [Microsoft DwmExtendFrameIntoClientArea](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmextendframeintoclientarea)
