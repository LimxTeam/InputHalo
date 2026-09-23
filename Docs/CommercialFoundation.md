# InputHalo 商业版本基础

产品：InputHalo；团队：LimxTeam。
权利主体：佛山市壹贰冉冉科技有限公司 / Foshan Yier Ranran Technology Co., Ltd.



## 当前已接入

- 版本统一由 CMake 的 `project(VERSION)` 生成 `ProductInfo.h`；当前 1.0.0 Preview。
- EXE 中英文 VERSIONINFO：ProductName、FileDescription、CompanyName、FileVersion、ProductVersion、OriginalFilename、版权、LimxTeam 开发声明。
- 任务栏 AppUserModelID：`LimxTeam.InputHalo`；可执行文件名 `InputHalo.exe`。
- 普通运行权限 `asInvoker`，`uiAccess=false`，不为了展示公司名称索取管理员权限。
- Windows 10/11 兼容性、Per-Monitor V2 DPI、长路径声明。
- “关于”页：产品、专有软件许可、完整第三方开源声明、隐私说明；支持离线阅读。
- 编译输出附带 LICENSE.txt、THIRD_PARTY_NOTICES.txt、PRIVACY.txt、Licenses/。
- 应用的专有声明不覆盖第三方许可证赋予的权利。

## Windows 各显示位置的来源

| 位置 | 来源 | 当前状态 |
| --- | --- | --- |
| 文件资源管理器里的 EXE 名 | 文件名 | InputHalo.exe |
| 文件属性：产品、描述、公司、版本 | PE VERSIONINFO | 已写入中英文信息 |
| 任务管理器的友好名称/描述 | FileDescription / Windows 的显示方式 | 已提供 InputHalo；详情页进程名仍为 InputHalo.exe |
| 任务栏分组 | AppUserModelID、图标 | LimxTeam.InputHalo / 自有图标 |
| 提权提示的程序名称 | Windows 对 EXE 描述和签名的读取 | 元数据已提供；正常启动不主动请求提权 |
| “已验证的发布者” | 受信任 Authenticode 签名中的证书主体 | 尚未签名，不能通过修改 CompanyName 冒充 |
| 开始菜单、安装目录、卸载项 | 安装器与快捷方式元数据 | 尚未制作安装包，也未写入虚假的卸载注册项 |

建议后续按用户安装到 `%LOCALAPPDATA%\Programs\LimxTeam\InputHalo`，
开始菜单文件夹使用 `LimxTeam`，快捷方式使用 `InputHalo`，卸载项显示 InputHalo 和公司名称。
安装器现已按上述规范接入，具体行为和编译方式见 [安装包说明](Packaging.md)。
已生成本地安装包，未自动安装或正式发布。

## 正式发布还需要提供

1. 公司主体的受信任代码签名证书或签名服务。证书的真实主体必须与公司身份一致。
2. 正式用户授权协议和商业方案：购买方式、授权范围、设备数、转让及升级规则。
   当前 LICENSE 是权利保留和第三方许可证边界声明，不是完整交易条款，也没有激活服务。
3. 官方网站、支持邮箱与隐私联系渠道。用户未提供，当前没有编造这些信息。
4. 版本渠道和更新策略。以后若引入联网更新或授权，需要同步修订当前隐私说明。

`Tools/Sign-Release.ps1` 仅提供显式的签名入口；不会自动签名、生成自签名证书或改动系统信任设置。
签名应在所有二进制和资源修改完成后进行；后续修改文件会破坏签名。

参考：
- [VERSIONINFO](https://learn.microsoft.com/en-us/windows/win32/menurc/versioninfo-resource)
- [应用清单与权限](https://learn.microsoft.com/en-us/windows/win32/sbscs/application-manifests)
- [Windows 代码签名选项](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)

