/*******************************************************************************
 * 文件: PreferenceActions.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   处理用户在设置面板"配置预设库"中触发的各类动作：应用预设、删除预设、
 *   保存预设到文件、导入预设、导出预设、添加排除应用。
 *   包含文件选择对话框调用、JSON 配置读写、预设文件管理（重名自动加序号）、
 *   内置/用户预设区分等逻辑。
 *   算法复杂度：文件遍历 O(N)，N 为预设文件数；重名检测为顺序扫描。
 *   空间-时间权衡：每次操作后全量重新读取预设列表，逻辑简单但 I/O 稍多；
 *   考虑到预设数量有限（上限 128 个），可接受。
 *
 * 设计哲学:
 *   所有用户触发的预设库操作集中在一个函数中处理，通过枚举区分动作类型。
 *   对文件路径做安全校验（防止删除库目录外的文件），对预设数量做上限保护。
 *   操作结果通过面板状态栏文本反馈给用户，而非抛出异常中断主流程。
 *
 * 技术特性:
 *   - 安全路径校验: 删除预设前校验路径是否在库目录内，防止越权删除。
 *   - 重名自动处理: 保存时若目标文件已存在，自动追加序号后缀。
 *   - 预设数量上限: 限制最多 128 个用户预设，防止配置膨胀。
 *   - 内置预设保护: 标记为 builtin 的预设不可删除。
 *
 * 依赖关系:
 *   内部: ih::platform (Win32Preferences 文件选择对话框)、ih::ConfigStore (SaveConfig)、
 *         ih::SettingsModel、ih::ui::LibraryPanel
 *   外部: Win32 文件对话框 API、std::filesystem
 *
 * 注意事项:
 *   - Windows 平台专用（文件选择对话框）。
 *   - 所有操作失败均通过 panel.status 反馈，返回值表示是否需要标记面板已修改。
 *
 ******************************************************************************/
// PreferenceActions 头文件声明
#include "App/PreferenceActions.h"
// Win32 偏好设置：文件选择对话框
#include "Platform/Windows/Win32Preferences.h"
// 配置存储：SaveConfig 写入 JSON 文件
#include "Core/Config/ConfigStore.h"
// 标准算法与文件系统库
#include <algorithm>
// ih 命名空间
namespace ih {
// ========================================================================
// HandlePreferenceAction — 处理预设库面板的用户动作
// 参数: owner — 所有者窗口句柄; directory — 预设库目录; panel — 库面板引用
//       action — 用户触发的动作枚举; settings — 当前设置模型引用
// 返回: true 表示设置已变更（需标记面板已修改），false 表示无变更
// ========================================================================
bool HandlePreferenceAction(HWND owner,const std::filesystem::path& directory,ui::LibraryPanel& panel,ui::LibraryAction action,SettingsModel& settings){
// 使用枚举类枚举值，简化动作判断
    using enum ui::LibraryAction;
// 所有操作包裹在 try-catch 中，异常时在状态栏提示
    try{
// 获取当前语言与选中项索引
        const bool chinese=settings.values["general"]["language"]=="zh-CN";
// 判断是否有有效的选中项
        const int index=panel.Selected();
// --- 动作: 应用预设 --- 将选中预设的设置应用到当前模型
        const bool selected=index>=0&&index<static_cast<int>(panel.profiles.size());
// --- 动作: 删除预设 --- 仅允许删除用户自建预设（非 builtin）
        if(action==ApplyProfile){if(selected){ih::ApplyProfile(settings,panel.profiles[index].settings);panel.status="Preset applied";return true;}return false;}
// 安全校验：确认预设文件在库目录内，防止越权删除
        if(action==DeleteProfile){
// 删除文件后重新加载预设列表并选中第一项
            if(selected&&!panel.profiles[index].builtin){
// 导出预设必须有选中项
                const auto path=panel.profiles[index].path;
// 保存/导入预设时检查预设数量上限（128 个）
                if(path.parent_path()!=directory)throw std::runtime_error("Profile is outside the local library");
// 弹出文件选择对话框；保存/导出为保存模式，添加应用为可执行文件选择
                std::filesystem::remove(path);panel.profiles=ReadProfiles(directory);panel.Select(0);panel.status="Preset deleted";
// 对话框打开失败
            }return false;
// 用户取消选择
        }
// 获取用户选中的文件路径
        if(action==ExportProfile&&!selected)return false;
// --- 动作: 添加排除应用 --- 将选中的可执行文件名加入排除列表
        if((action==SaveProfile||action==ImportProfile)&&panel.profiles.size()>=131){panel.status="Preset limit reached (128)";return false;}
// 提取文件名（UTF-8），追加到排除应用列表，重建设置模型
        const auto picked=platform::PickPreferenceFile(owner,action==SaveProfile||action==ExportProfile,action==AddApplication,chinese,action==SaveProfile?directory:std::filesystem::path{});
// 获取当前设置的快照（作为保存/导入的基础）
        if(!picked){panel.status="Could not open the file picker";return false;}
// --- 动作: 导入预设 --- 从用户选择的 JSON 文件导入设置
        if(!*picked){panel.status="Cancelled";return false;}
// --- 动作: 导出预设 --- 导出选中预设到用户选择的位置
        const auto path=**picked;
// 保存预设到用户指定路径；导出后直接返回（不加入本地库）
        if(action==AddApplication){
// 计算目标路径：在库目录中以预设文件名命名，扩展名统一为 .json
            auto name=path.filename().u8string();auto value=std::string(reinterpret_cast<const char*>(name.data()),name.size());
// 若目标路径与源路径不同且已存在，自动追加序号后缀避免覆盖
            auto& apps=settings.values["exclusions"]["apps"];apps.push_back(value);settings=SettingsModel::FromJson(settings.values);
// 保存到最终目标路径
            panel.status="Application exclusion updated";return true;
// 重新加载预设库列表，并选中刚导入/保存的预设
        }
// 设置状态栏提示文本
        auto snapshot=ProfileSettings(settings);
// 捕获所有异常：在状态栏提示操作失败
        if(action==ImportProfile){auto imported=ih::ImportProfile(path);if(!imported){panel.status="Invalid preset: use an InputHalo JSON file (up to 256 KiB)";return false;}snapshot=std::move(*imported);}
        if(action==ExportProfile)snapshot=ProfileSettings(panel.profiles[index].settings);
        if(action==SaveProfile||action==ExportProfile){
            if(auto saved=SaveConfig(path,snapshot);!saved){panel.status="Could not save preset";return false;}
            if(action==ExportProfile){panel.status="Preset exported";return false;}
        }
        auto destination=directory/path.filename();destination.replace_extension(L".json");
        if(destination!=path){
            const auto stem=destination.stem().wstring();unsigned suffix=2;
            while(std::filesystem::exists(destination))destination=directory/(stem+L" ("+std::to_wstring(suffix++)+L").json");
            if(auto saved=SaveConfig(destination,snapshot);!saved){panel.status="Could not save preset";return false;}
        }
        panel.profiles=ReadProfiles(directory);
        for(size_t i=0;i<panel.profiles.size();++i)if(panel.profiles[i].path==destination)panel.Select(static_cast<int>(i));
        panel.status=action==ImportProfile?"Preset imported; select Apply to use it":"Current setup saved";
    }catch(const std::exception&){panel.status="Could not update preset library";}
    return false;
}
}
