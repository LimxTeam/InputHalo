# Milestone 4：基础文字渲染

2026-09-23。实现 FreeType 2.14.3、HarfBuzz 14.2.0、动态 R8 图集和 Vulkan 文字绘制。

## 路径与接口

`UTF-8 → TextEngine → HarfBuzz glyph/offset/advance → FreeType 灰度字形 → Layout → Painter.Text → VulkanRenderer2D`

TextEngine 使用 HarfBuzz 原生 OpenType 字体函数排版；FreeType 按对应像素字号做无 hinting 的灰度光栅化。
字体以文件内容加载，避免 FreeType 路径编码问题。系统字体通过 Windows Fonts known folder 发现，
优先 Segoe UI，并按字形覆盖回退到已安装的 Microsoft YaHei、SimSun、Arial；不分发这些字体。
也可通过 `AddFont(path, faceIndex)` 显式添加字体。依赖许可与归属见 `ThirdParty/README.md`。

```cpp
ih::text::TextEngine text;
for (const auto& path : ih::platform::DefaultFontPaths()) text.AddFont(path);
auto layout = text.Shape("InputHalo 输入光晕", 22, dpi / 96.f);
ih::render2d::Painter painter(widthInDip, heightInDip);
painter.Text(layout, {36, 40}, {1, 1, 1, 1});
```

布局坐标为 DIP；字号乘缩放后四舍五入到物理像素，字形缓存键包含字体 ID、glyph index 和像素字号。
支持显式换行、空格 advance、组合字符以及同方向脚本 run 的 HarfBuzz shaping。
严格拒绝非法 UTF-8。缺字绘制 `.notdef`，通过 `Layout::missingGlyphs` 报告，不静默声称字体覆盖完整。

图集默认 2048×2048，灰度 R8_UNORM，每字形留一像素间隔，shelf packing，最多 8 页。
需要时增加页面；超过容量或单个字形超页明确报错，不覆盖旧字形。布局持有不可变快照，
新增字形时按需复制；同一个引擎的较新快照兼容旧 glyph UV，Painter 可提升 DrawList 的快照版本。
不同引擎的图集不可混入同一 DrawList。

每个 GPU 帧拥有独立图集纹理数组和 staging buffer。仅快照变化时准备上传，帧 fence 完成后才可覆写，
复制命令在 rendering 之前执行，明确转换到 fragment sampled-read。稳定帧不重复上传。
当前更新会上传整个快照，尚未实现脏矩形上传；8 页满容量时每份图集像素为 32 MiB，帧副本和 staging 另计。
图形与字形共用有序实例绘制与裁剪，字形采样 R8 coverage 后输出预乘 alpha。

设置窗口已经显示英文标题、说明和中文内容。窗口尺寸/DPI 变化会重新排版；静止时继续消息等待。
字体/Shader 预检查失败记录 Text disabled，保留原有设置窗口；Overlay 仍使用圆形验收场景。

## 验证

本机 Windows 11 / RTX 3060，使用已安装的系统字体：

- Debug/Release 构建通过；四项 CTest（Text、Renderer2D、Config、OverlayAlphaPolicy）通过。
- 字体测试：ASCII、中文回退、希腊文、阿拉伯文 RTL run、组合重音、换行、空串、空格、非法 UTF-8、缺字计数。
- 缓存命中不改变图集快照；新增 glyph 不修改旧布局；小图集强制分页通过。
- Debug/Release GPU 测试：100/125/150/200% 渲染缩放下，文字采样、裁剪和 alpha 与 CPU 图集双线性参考逐像素比较通过。
- 多页 R8 纹理数组采样通过；形状渲染、sRGB 混合及双屏 Overlay 回归通过。
- Debug 带文字窗口测试 120 帧通过：12 次 resize/minimize/restore，期间替换图集和渲染缩放，验证资源寿命。
- Debug 同步验证 0 错误、0 警告。常规主程序的 OBS 注入层版本警告仍单独存在。
- Release 窗口回归的一次执行被 Windows Application Control 阻止；随后补充了带文字窗口测试并完成编译，Release 的该项运行验收仍保留。
- 主程序中英文画面和四档离屏文字预览已查看，测试窗口已关闭。

```powershell
ctest --preset debug
ctest --preset release
.\Tools\Test-Renderer2D.ps1 -Configuration Debug
.\Tools\Test-Renderer2D.ps1 -Configuration Release
.\Tools\Test-Vulkan.ps1 -Configuration Debug
```

文字 GPU 测试复用 Renderer2D 冒烟程序，日志在 `Build/windows-msvc/renderer2d-smoke-logs`；
预览在 `Build/windows-msvc/renderer2d-results/text-{100,125,150,200}.bmp`，棋盘仅用于查看透明区域。
Release Text CTest 曾出现一次启动失败，直接运行及随后 CTest 均已通过；未改动系统安全策略。

## 当前边界

这是字体 shaping 和基础行布局，不是完整国际化段落系统：尚无 UAX #9 混合双向段落重排、
自动折行、制表位、文本编辑/命中测试、复杂 emoji/彩色字体、跨字体 grapheme fallback。
HarfBuzz 处理脚本 run 内的复杂 shaping，不能用本次单 RTL run 测试宣称任意双向文本已支持。
实际混合 DPI 屏幕切换、其他 GPU 和长文本性能仍待验收；DrawList 继续受 4096 命令容量限制。
下一里程碑为输入采集（Raw Input）。

参考：[HarfBuzz shaping](https://harfbuzz.github.io/shaping-and-shape-plans.html)、
[FreeType](https://freetype.org/)。固定版本的构建与许可以下载到 `_deps` 的源码为准。
