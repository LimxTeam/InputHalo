/*******************************************************************************
 * 文件: ProfileStore.cpp
 * 创建时间: 2026-09-23
 * 创建者: LimxTeam
 *
 * 功能描述:
 *   实现方案的应用合并、外部 JSON 导入校验，以及内置/用户方案目录枚举。
 *   内置 Teaching/Meeting/Recording 三套预设在此直接构造；导入路径做大小、嵌套深度、版本与字段类型白名单校验。
 *   复杂度: 导入 O(方案字段数)；枚举 O(目录文件数)，目录列表一次性缓存。
 *
 * 设计哲学:
 *   以 SettingsModel::Defaults() 作为白名单模板——只接受默认模型中已存在的键且类型匹配，
 *   未知键静默忽略，从而对未来字段前向兼容，也拒绝任意恶意 JSON。
 *
 * 技术特性:
 *   - JSON 解析限制嵌套深度 ≤16，防止深度嵌套 DoS
 *   - 目录扫描上限 128 个文件并按文件名排序，保证稳定顺序
 *   - UTF-8 文件名按字节原样保存
 *
 * 依赖关系:
 *   内部: Settings/SettingsModel.h(及 Input/Keys.h 经 SettingsModel)
 *   外部: nlohmann/json、<fstream>、<algorithm>
 *
 * 注意事项:
 *   导入失败统一走 std::expected 字符串错误；解析前校验文件大小以防解析期间被改写。
 ******************************************************************************/
#include "Settings/ProfileStore.h"
#include <fstream>
#include <algorithm>
// InputHalo 配置层命名空间实现。
namespace ih {
// 用默认值做底，再用 source 覆盖，返回一份可独立使用的设置副本。
SettingsModel ProfileSettings(const SettingsModel& source){
    auto result=SettingsModel::Defaults();ApplyProfile(result,source);return result;
}
// 受控合并：仅拷贝可视化相关分区与强调色，本机状态保持不变。
void ApplyProfile(SettingsModel& target,const SettingsModel& source){
// 这四个分区属于“外观预设”，随方案一起迁移。
    for(const char* section:{"input","cursor","laser","drawing"})target.values[section]=source.values[section];
// 强调色属于界面外观，也随方案迁移；其余 general 项不迁移。
    target.values["general"]["accent"]=source.values["general"]["accent"];
    // Startup, language and local exclusion policies belong to the machine, not the visual preset.
}
// 导入入口：包裹在 try 中，任何异常都转为 expected 错误。
std::expected<SettingsModel,std::string> ImportProfile(const std::filesystem::path& path){
// 解析前的硬性安全检查块。
    try{
// 限制方案体积 256 KiB，避免超大文件消耗内存。
        if(std::filesystem::file_size(path)>256*1024)return std::unexpected("Profile file is too large");
// 以二进制方式打开文件。
        std::ifstream stream(path,std::ios::binary);if(!stream)return std::unexpected("Cannot read profile");
// 流式解析并限制嵌套深度，超过 16 层直接抛错。
        auto source=nlohmann::json::parse(stream,[](int depth,nlohmann::json::parse_event_t,const nlohmann::json&){if(depth>16)throw std::runtime_error("Profile is too deeply nested");return true;});
// 校验根为对象且版本号恰为 1，否则判定格式不支持。
        if(!source.is_object()||!source.contains("version")||source["version"]!=1)return std::unexpected("Unsupported profile format");
// 取默认值作为字段白名单与类型基准。
        const auto defaults=SettingsModel::Defaults();
// 逐分区校验：每个分区必须存在且为对象。
        for(const char* section:{"input","cursor","laser","drawing"}){
            if(!source.contains(section)||!source[section].is_object())return std::unexpected("Incomplete profile");
// 遍历该分区的每个字段。
            for(const auto& [key,value]:source[section].items()){
// 白名单过滤：默认值中不存在的键直接跳过。
                if(!defaults.values[section].contains(key))continue;
                const auto& expected=defaults.values[section][key];
// 类型校验：要求类型一致，或双方均为数字(允许 int/double 互通)。
                if(!(value.type()==expected.type()||(value.is_number()&&expected.is_number())))return std::unexpected("Invalid profile field type");
            }
        }
// 校验通过后转换为规范化设置并返回。
        return ProfileSettings(SettingsModel::FromJson(source));
// 兜底：任意解析异常都转为可读错误信息。
    }catch(const std::exception&){return std::unexpected("Invalid profile file");}
}
// 枚举方案：先放内置预设，再扫描用户目录。
std::vector<Profile> ReadProfiles(const std::filesystem::path& directory){
    std::vector<Profile> result;
// 内置“教学演示”预设即默认值本身。
    auto teaching=SettingsModel::Defaults();result.push_back({"Teaching",{},teaching,true});
// 内置“会议简洁”：缩小缩放、减小光标、关闭激光。
    auto meeting=teaching;meeting.values["input"]["scale"]=.85;meeting.values["cursor"]["size"]=30;meeting.values["cursor"]["glow"]=0;meeting.values["laser"]["enabled"]=false;result.push_back({"Meeting",{},meeting,true});
// 内置“录屏清晰”：放大缩放、延长停留、换暖色光标。
    auto recording=teaching;recording.values["input"]["scale"]=1.2;recording.values["input"]["hold_ms"]=1000;recording.values["cursor"]["color"]="#FFCB78";result.push_back({"Recording",{},recording,true});
// 确保用户方案目录存在(忽略错误)。
    std::error_code ec;std::filesystem::create_directories(directory,ec);
// 收集目录下的 .json 路径。
    std::vector<std::filesystem::path> paths;
// 遍历目录条目(带错误码版本，不抛异常)。
    for(std::filesystem::directory_iterator it(directory,ec),end;!ec&&it!=end;it.increment(ec))
// 仅接纳常规文件、.json 扩展名，且总数不超过 128。
        if(it->is_regular_file(ec)&&it->path().extension()==L".json"&&paths.size()<128)paths.push_back(it->path());
// 按路径排序，使方案顺序稳定可预测。
    std::sort(paths.begin(),paths.end());
// 逐个尝试导入，仅接纳校验通过的文件。
    for(const auto& path:paths)if(auto settings=ImportProfile(path)){
// 用文件主干名(UTF-8)作为方案名，记录来源路径，标记为用户方案。
        const auto name=path.stem().u8string();result.push_back({std::string(reinterpret_cast<const char*>(name.data()),name.size()),path,std::move(*settings),false});
    }
    return result;
}
}
