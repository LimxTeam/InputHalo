# InputHalo

由 **LimxTeam** 开发的 Windows 输入可视化与演示标注工具。
权利主体：**佛山市壹贰冉冉科技有限公司 / Foshan Yier Ranran Technology Co., Ltd.**
当前版本：**1.0.0 Preview**。专有商业软件，保留所有权利。

## 当前功能

- 跟随指针的键盘、鼠标按键和滚轮提示，支持屏幕边缘避让。
- 指针光晕、点击波纹、激光轨迹及可自定义的显示样式。
- 按住快捷键后连续框选标注，松开快捷键自动清除。
- 矩形、箭头和椭圆标注；保持快捷键时右键撤销上一笔。
- 本地配置方案、三种内置预设、JSON 导入导出。
- 按前台程序排除全部提示，以及按精确快捷键组合隐藏显示。
- 紧凑的中英文设置界面、自绘标题栏、实时窗口缩放与系统 DPI 适配。
- 关闭设置窗口收起到托盘；托盘支持打开、暂停和退出。
- 本地配置保存、当前用户开机启动选项。
- “关于”页内置产品信息、软件许可、第三方许可证全文和隐私说明。

## 构建与运行

需要 Windows 10/11 x64、CMake 4.4.3、Visual Studio 2026 C++ 桌面工具及 Windows SDK，
以及 Vulkan SDK **1.4.357.0**。运行需要支持 Vulkan 1.4 的显卡驱动。
首次配置联网获取固定版本的 spdlog、nlohmann/json、FreeType 和 HarfBuzz。
字体读取 Windows 已安装字体，不随程序分发；中文显示需要 Microsoft YaHei 或 SimSun。

```powershell
$env:VULKAN_SDK = 'C:\Application\Development\VulkanSDK\1.4.357.0'
cmake --preset windows-msvc
cmake --build --preset debug --target InputHalo
.\Build\windows-msvc\Debug\InputHalo.exe
```

日常使用建议编译并运行 `Release`（`cmake --build --preset release --target InputHalo`，
启动 `Build/windows-msvc/Release/InputHalo.exe`）。Debug 带 Vulkan 验证层，内存不可作为交付版本基线。
这是本地编译，不会生成安装包。新增功能用法见 [方案与排除规则](Docs/PresetsAndExclusions.md)，
内存分析见 [内存占用记录](Docs/MemoryFootprint.md)。

采用 C++23、静态 VC Runtime 和自有 UI/Renderer2D。Vulkan 绘制共享纹理，
D3D11/DirectComposition 完成透明桌面合成。运行目录须保留生成的 `Shaders/`。
正常启动使用普通用户权限；程序初始化已在清单声明 Per-Monitor V2 DPI。
已提供 Inno Setup 安装脚本，运行 `Tools/Build-Installer.ps1` 在 `Build/Installer` 生成安装包。
安装行为见 [安装包说明](Docs/Packaging.md)。当前尚未使用受信任证书签名。

## 许可证与商业信息

- [软件权利声明](LICENSE)：专有商业软件声明，第三方组件不受本软件专有条款覆盖。
- [第三方声明全文](THIRD_PARTY_NOTICES.txt) 与 [依赖清单](ThirdParty/README.md)。
- [隐私说明](Docs/Privacy.txt)：当前输入事件仅在内存中处理，不保存键入历史。
- [商业版本基础与 Windows 身份说明](Docs/CommercialFoundation.md)。
- [后续功能评估](Docs/FeatureAssessment.md)。

编译输出同时保留 `LICENSE.txt`、`THIRD_PARTY_NOTICES.txt`、`PRIVACY.txt` 和 `Licenses/`。
修改依赖或许可后运行 `Tools/Update-Notices.ps1` 并重新编译以更新内置声明。
签名工具入口为 `Tools/Sign-Release.ps1`，需要另行提供真实的签名证书，不会自动执行。

## 数据与技术记录

配置：`%LOCALAPPDATA%\InputHalo\settings.json`。
诊断日志：`%LOCALAPPDATA%\InputHalo\logs\inputhalo.log`，每份 5 MB，共 3 份。
损坏配置会保留为 `settings.json.invalid[.N]`。快捷键模式不等于密码框识别。

技术背景见 [规格书](InputHalo_Technical_Specification.md)、
[透明合成记录](Docs/OverlayComposition.md)、[Renderer2D](Docs/Renderer2D.md)、
[文字渲染](Docs/TextRendering.md)。这些文档中的早期里程碑验证不代表当前版本完成了全部兼容性验收。

Portions of this software are copyright © 2026 The FreeType Project (https://freetype.org). All rights reserved.
