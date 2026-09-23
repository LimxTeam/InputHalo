# 系统窗口与安全桌面的指针跟随

用户报告普通使用正常，操作安装程序或权限请求窗口时光圈卡顿。
排查发现原主循环在没有 Raw Input 消息、没有动画时可等待 250 ms。
若特定窗口或桌面切换导致消息减少，即使仍可读取指针位置，光圈也可能随慢速唤醒刷新。
本轮未交互复现具体安装程序，不能断言这是所有系统窗口卡顿的唯一原因。

修正内容：
- 光圈启用且桌面可访问时，每隔约 16 ms 主动采样位置，不依赖鼠标消息。
- 使用高精度可等待计时器，不修改全局计时器精度；不支持时回退消息等待。
- 待呈现帧短间隔重试，避免多等待一个 16 ms 周期。
- 检查 GetCursorPos 的失败结果，不把失败当成 (0,0) 坐标。
- 约 50 ms 检查桌面可访问状态；不可访问时隐藏、清理提示，恢复时重建叠加呈现资源，
  防止悬挂旧帧的呈现超时影响恢复。

继续使用普通用户权限。屏幕变暗的 UAC 安全桌面期间不绘制光圈，不关闭 UAC 或修改安全策略。
此次修正已编入安装包；未运行自动化或交互测试。

参考：
- https://learn.microsoft.com/en-us/windows/security/application-security/application-control/user-account-control/how-it-works
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getcursorpos
