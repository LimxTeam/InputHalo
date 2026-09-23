# Milestone 2：Windows 透明合成接入

2026-09-23。主应用接入实验验证过的共享纹理路径，保留原始技术规格书作为设计基线。

后续 M3 更新：单圆 compute 已替换为通用 SDF 图形管线，共享纹理改为 color attachment。
最新实现及验证见 [Renderer2D](Renderer2D.md)。以下保留 M2 当时的路径与验收记录。

## 路径选择

本机直接 Win32 Vulkan Surface 仅支持 OPAQUE，六种窗口/DWM 配置都无法提供透明交换链。
因此 Overlay 偏离规格中的直接 VkSwapchain 呈现路径，采用：

`Vulkan compute → RGBA8 共享纹理 → D3D11 CopyResource → DirectComposition`

所有圆像素由 Vulkan 生成，D3D11 不执行绘制；没有 GDI 绘制、CPU 像素读回或桌面采样。
设置窗口继续使用 Vulkan Swapchain，与所有 Overlay 共享同一个 Vulkan 实例、设备和队列。
设备选择优先支持外部纹理导入和 Win32 keyed mutex 的图形/计算队列，按 LUID 匹配 DXGI 适配器。
不支持时保留设置窗口，不创建不透明覆盖层。前期证据见两个实验目录中的报告。

## 生命周期

- `OverlayManager` 管理每屏窗口、独立呈现目标及共享圆形管线。
- 每屏两个导入纹理；Vulkan 获取 key 0、释放 key 1，D3D11 复制后释放 key 0。
- GPU fence、D3D11 mutex、Present 以非阻塞方式查询；未完成时消息循环最多每 16ms 重试。
- 首帧呈现成功后显示窗口。圆固定在各屏中心，半径 48 DIP、alpha 0.85；配置尚不控制该验收圆。
- 空闲后进入消息等待。显示布局、Overlay DPI、DWM 合成变化及恢复运行消息会重建全部目标。
- 重建/退出先隐藏窗口，再等待 GPU 并释放导入资源，最后释放原生纹理、窗口和共享设备。
- Shader 缺失、互操作不兼容及呈现超时会关闭 Overlay，标题提示错误，细节写入 App 日志。
- Vulkan 设备丢失退出；当前没有完整的设备重建机制。窗口标题提示仅为骨架阶段错误入口。

## 本机验证

Windows 11、RTX 3060、Vulkan API 1.4.325，两块 1920×1080、96 DPI 显示器。

- Debug / Release 构建和两项 CTest 通过。
- 原有设置窗口 Vulkan 冒烟测试 Debug / Release 各 120 帧通过。
- 新 Overlay Debug 测试通过：每屏首帧、空闲零提交、八次双缓冲重绘、三次提交中重建。
- 注入显示布局/恢复运行消息后重建；缺失 Shader 初始化失败、清理及恢复均通过。
- Debug 同步验证 0 错误、0 警告（测试进程隔离旧 OBS 注入层）。
- Debug 主程序桌面实测：两屏圆均可见、圆外背景透明；在独立进程测试窗口上点击圆，点击计数从 0 到 1，再移动到底部副屏点击后到 2。
- Release Overlay 测试可执行文件被 Windows Application Control 阻止启动，未绕过策略，运行验收待完成。

硬件测试通过 `Tools/Test-Overlay.ps1` 显式运行，日志位于 `Build/windows-msvc/overlay-smoke-logs`。
消息注入不等于真实热插拔/睡眠验证；其他 GPU、混合 DPI、HDR 和跨适配器显示仍待验证。
下一里程碑为 Renderer2D，不在本次接入中扩展完整 UI 或输入采集。
