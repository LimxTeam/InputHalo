#include "Settings/ProfileStore.h"
#include <fstream>
#include <algorithm>
namespace ih {
SettingsModel ProfileSettings(const SettingsModel& source){
    auto result=SettingsModel::Defaults();ApplyProfile(result,source);return result;
}
void ApplyProfile(SettingsModel& target,const SettingsModel& source){
    for(const char* section:{"input","cursor","laser","drawing"})target.values[section]=source.values[section];
    target.values["general"]["accent"]=source.values["general"]["accent"];
    // Startup, language and local exclusion policies belong to the machine, not the visual preset.
}
std::expected<SettingsModel,std::string> ImportProfile(const std::filesystem::path& path){
    try{
        if(std::filesystem::file_size(path)>256*1024)return std::unexpected("Profile file is too large");
        std::ifstream stream(path,std::ios::binary);if(!stream)return std::unexpected("Cannot read profile");
        auto source=nlohmann::json::parse(stream,[](int depth,nlohmann::json::parse_event_t,const nlohmann::json&){if(depth>16)throw std::runtime_error("Profile is too deeply nested");return true;});
        if(!source.is_object()||!source.contains("version")||source["version"]!=1)return std::unexpected("Unsupported profile format");
        const auto defaults=SettingsModel::Defaults();
        for(const char* section:{"input","cursor","laser","drawing"}){
            if(!source.contains(section)||!source[section].is_object())return std::unexpected("Incomplete profile");
            for(const auto& [key,value]:source[section].items()){
                if(!defaults.values[section].contains(key))continue;
                const auto& expected=defaults.values[section][key];
                if(!(value.type()==expected.type()||(value.is_number()&&expected.is_number())))return std::unexpected("Invalid profile field type");
            }
        }
        return ProfileSettings(SettingsModel::FromJson(source));
    }catch(const std::exception&){return std::unexpected("Invalid profile file");}
}
std::vector<Profile> ReadProfiles(const std::filesystem::path& directory){
    std::vector<Profile> result;
    auto teaching=SettingsModel::Defaults();result.push_back({"Teaching",{},teaching,true});
    auto meeting=teaching;meeting.values["input"]["scale"]=.85;meeting.values["cursor"]["size"]=30;meeting.values["cursor"]["glow"]=0;meeting.values["laser"]["enabled"]=false;result.push_back({"Meeting",{},meeting,true});
    auto recording=teaching;recording.values["input"]["scale"]=1.2;recording.values["input"]["hold_ms"]=1000;recording.values["cursor"]["color"]="#FFCB78";result.push_back({"Recording",{},recording,true});
    std::error_code ec;std::filesystem::create_directories(directory,ec);
    std::vector<std::filesystem::path> paths;
    for(std::filesystem::directory_iterator it(directory,ec),end;!ec&&it!=end;it.increment(ec))
        if(it->is_regular_file(ec)&&it->path().extension()==L".json"&&paths.size()<128)paths.push_back(it->path());
    std::sort(paths.begin(),paths.end());
    for(const auto& path:paths)if(auto settings=ImportProfile(path)){
        const auto name=path.stem().u8string();result.push_back({std::string(reinterpret_cast<const char*>(name.data()),name.size()),path,std::move(*settings),false});
    }
    return result;
}
}
