#include "App/PreferenceActions.h"
#include "Platform/Windows/Win32Preferences.h"
#include "Core/Config/ConfigStore.h"
#include <algorithm>
namespace ih {
bool HandlePreferenceAction(HWND owner,const std::filesystem::path& directory,ui::LibraryPanel& panel,ui::LibraryAction action,SettingsModel& settings){
    using enum ui::LibraryAction;
    try{
        const bool chinese=settings.values["general"]["language"]=="zh-CN";
        const int index=panel.Selected();
        const bool selected=index>=0&&index<static_cast<int>(panel.profiles.size());
        if(action==ApplyProfile){if(selected){ih::ApplyProfile(settings,panel.profiles[index].settings);panel.status="Preset applied";return true;}return false;}
        if(action==DeleteProfile){
            if(selected&&!panel.profiles[index].builtin){
                const auto path=panel.profiles[index].path;
                if(path.parent_path()!=directory)throw std::runtime_error("Profile is outside the local library");
                std::filesystem::remove(path);panel.profiles=ReadProfiles(directory);panel.Select(0);panel.status="Preset deleted";
            }return false;
        }
        if(action==ExportProfile&&!selected)return false;
        if((action==SaveProfile||action==ImportProfile)&&panel.profiles.size()>=131){panel.status="Preset limit reached (128)";return false;}
        const auto picked=platform::PickPreferenceFile(owner,action==SaveProfile||action==ExportProfile,action==AddApplication,chinese,action==SaveProfile?directory:std::filesystem::path{});
        if(!picked){panel.status="Could not open the file picker";return false;}
        if(!*picked){panel.status="Cancelled";return false;}
        const auto path=**picked;
        if(action==AddApplication){
            auto name=path.filename().u8string();auto value=std::string(reinterpret_cast<const char*>(name.data()),name.size());
            auto& apps=settings.values["exclusions"]["apps"];apps.push_back(value);settings=SettingsModel::FromJson(settings.values);
            panel.status="Application exclusion updated";return true;
        }
        auto snapshot=ProfileSettings(settings);
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
