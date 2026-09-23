# Milestone 3：Renderer2D

2026-09-23，按规格第 20–23、71–74、99 节搭建基础形状渲染。

## API 与边界

`Render2D/Painter` 不依赖 Win32 或 Vulkan，生成 POD Command 和有序 DrawList。
`RHI/Vulkan/VulkanRenderer2D` 消费 DrawList，使用 Dynamic Rendering、实例化四边形、
解析 SDF、fwidth/smoothstep 抗锯齿和预乘 alpha 混合。

```cpp
ih::render2d::Painter painter(widthInDip, heightInDip);
painter.RoundedRect({20, 20, 160, 80}, 12, {0.2f, 0.4f, 0.8f, 1});
painter.PushClip({30, 30, 120, 60});
painter.Circle({90, 60}, 32, {1, 1, 1, 0.5f});
painter.Line({30, 80}, {140, 40}, 3, {1, 0.6f, 0, 1});
painter.PopClip();
const auto& drawList = painter.List();
```

还提供 `FillRect`。所有坐标、圆角半径、线宽均为 DIP，由目标传入缩放系数；线段使用圆头，
重合端点绘制圆点，零宽线不产生像素。无效几何/非有限颜色抛出异常，颜色限幅至 0–1。
裁剪栈取嵌套交集，根裁剪不可弹出；矩形裁剪按物理像素中心转换为 Vulkan scissor。

只合并相邻、同裁剪的命令，不按形状重新排序，以保持半透明元素的绘制顺序。
本阶段共用一条 shape pipeline，无纹理切换。未来文字和图片将增加对应的批次条件。
每份 DrawList 最多 4096 条命令，超限明确报错；上传缓冲区每帧 256 KiB，持久映射且 host coherent。
缓冲区由帧资源持有，提交完成后才能覆写或释放。调用 Record 时，目标需已经开始兼容格式的 rendering。

Overlay 已用此管线替换 M2 单圆 compute shader：窗口仍显示每屏一个验收圆。
共享纹理使用 COLOR_ATTACHMENT，D3D11 仍仅做复制，窗口穿透/重建/空闲等待机制保持原有职责。
设置窗口尚无 UI 内容；管线可为 UNORM 或 sRGB attachment 创建，供后续设置 UI 复用。

## 色彩

输入为 straight sRGB。UNORM 合成目标输出 sRGB 数值的预乘 alpha，沿用 Windows 合成路径；
sRGB attachment 将输入先转换为线性值，在 attachment 上进行线性混合后编码。
两类目标在半透明重叠区域的结果不完全相同，测试分别验证各自的约定；没有宣称 HDR 或色彩管理支持。

## 验证

本机 Windows 11、RTX 3060、Vulkan 1.4.325：

- Debug/Release 构建与 CTest：配置、Overlay alpha 策略、Renderer2D 三项通过。
- CPU 测试覆盖批次顺序、嵌套裁剪、空裁剪、圆角限幅、非法输入、零宽线和容量边界。
- GPU 测试在 100/125/150/200% 缩放分别渲染同一 DIP 场景：10 图形、4 次绘制。
- 每档 14 个像素探针检查图形内部/外部、透明清屏、预乘 alpha 顺序、圆头/退化线段及嵌套裁剪。
- 另检查边缘分数覆盖率和 sRGB attachment 的颜色回转/线性透明混合。
- 四档 BMP 预览已逐一查看；预览以棋盘背景合成，断言直接使用原始 RGBA，不以预览替代数值验证。
- Debug 同步验证 0 错误、0 警告；Debug/Release 双屏 Overlay 回归通过。
- 本轮新的 Release 测试程序正常运行，没有修改系统 Application Control 策略。

运行 `Tools/Test-Renderer2D.ps1 -Configuration Debug` 或 `Release`。
日志：`Build/windows-msvc/renderer2d-smoke-logs/inputhalo.log`。
图像：`Build/windows-msvc/renderer2d-results/{100,125,150,200}.bmp`。
读回仅存在于测试程序，产品渲染路径不读回像素。

这些是四档离屏渲染缩放验证，尚未替代真实混合 DPI 显示器切换验收。
其他厂商 GPU、HDR、性能压力测试仍待验证。边框、纹理、文字、完整 UI 控件不在本阶段实现中。
下一里程碑为 FreeType、HarfBuzz、字体图集与文字布局。
