# Windows 安装包

使用 Inno Setup 7.1.0，当前编译器：`C:\Application\Development\Inno Setup 7\ISCC.exe`。

```powershell
.\Tools\Build-Installer.ps1
```

脚本先编译 Release 主程序，再编译 `Packaging/InputHalo.iss`，生成安装 EXE 与 SHA-256 校验文件。
版本读取主程序的四段 FileVersion，输出目录 `Build/Installer`。
编译器路径可通过 `-Compiler` 指定；已有最新 Release 时可以传入 `-SkipBuild`。

## 安装行为

- Windows 10 1809 或以上 / Windows 11，x64；需要支持 Vulkan 1.4 的显卡驱动，不需要 Vulkan SDK。
- 默认仅为当前用户安装到 `%LOCALAPPDATA%\Programs\LimxTeam\InputHalo`，不请求管理员权限。
- 中英文界面，InputHalo 图标、LimxTeam 开始菜单文件夹，可选桌面快捷方式。
- Windows 卸载列表显示 InputHalo，公司为佛山市壹贰冉冉科技有限公司。
- 仅包含程序、两个实际使用的着色器、许可与隐私文件；不包含测试程序、调试库、用户配置或日志。
- 同一 AppId 支持覆盖升级。安装/卸载前需要从托盘退出正在运行的 InputHalo；安装器不强杀进程。
- 卸载保留 `%LOCALAPPDATA%\InputHalo` 中的配置、方案及日志。
- 已有当前用户启动项会更新为安装后的程序路径；未开启的用户不会被自动开启。
  卸载仅删除指向本安装位置的 InputHalo 启动项。
- 安装完成后可选择启动程序；静默安装不会自动启动。

## 本次交付边界

Release 和安装脚本已编译成功，并生成校验文件。遵循用户要求，未执行安装/卸载或功能测试，
未自动安装到当前系统，也未上传或发布安装包。

安装包和主程序尚未使用公司受信任代码签名证书签名。
当前编译器输出 `Non-commercial use only`；正式商业发布前应核实 Inno Setup 的商业许可登记情况。
没有登记、购买或修改编译器授权，也没有改动其原有版权信息。

安装说明包含公司英文名称 Foshan Yier Ranran Technology Co., Ltd.，第三方声明随安装保留。
