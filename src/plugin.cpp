#include "logger.h"
#include "nlohmann/json.hpp";

using namespace RE;
using JSON = nlohmann::json;

struct Icon {
    std::string source;
    int frame = 1;
    float scale = 1.0;
};

static std::string readBuffer;

std::vector<Icon> icons;
std::unordered_map<std::string, int> data; // legacy icons by name
std::unordered_map<TESWorldSpace*, int> worldIcons;
std::unordered_map<TESObjectCELL*, int> cellIcons;

template <class T>
[[nodiscard]] static T* GetForm(const std::string_view& a_editorID) {
    const auto form = TESForm::LookupByEditorID(a_editorID);
    return form ? form->As<T>() : nullptr;
}

std::vector<std::string> getStringList(const nlohmann::json& j, const char* key) {
    std::vector<std::string> result;

    if (!j.contains(key)) return result;

    const auto& val = j.at(key);

    if (val.is_string()) {
        result.push_back(val.get<std::string>());
    } else if (val.is_array()) {
        for (const auto& e : val) {
            if (e.is_string()) {
                result.push_back(e.get<std::string>());
            }
        }
    }

    return result;
}

void CompileLocalIcons() {
    static bool initalized = false;
    if (initalized) return;
    std::vector<std::filesystem::path> files;

    // sort by ASC first
    for (const auto& entry : std::filesystem::directory_iterator("Data/SKSE/Plugins/LocalMapIcons")) {
        auto ext = entry.path().extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);
        if (ext == ".json") {
            files.push_back(entry.path());
        }
    }
    std::ranges::sort(files, {}, [](const auto& p) { return p.filename().string(); });

    for (const auto& path : files) {
        std::ifstream ifile{path};
        if (!ifile) continue;
        try {
            JSON json = JSON::parse(ifile);
            if (json.is_discarded()) continue;
            for (const auto& item : json) {
                if (!item.contains("icon")) continue;
                Icon icon{};

                auto& source = item.at("icon");
                if (source.is_string()) {
                    icon.source = source.get<std::string>();
                } else if (source.is_number_integer()) {
                    icon.source = std::to_string(source.get<int>());
                } else {
                    continue;
                }

                if (item.contains("scale")) icon.scale = item.at("scale").get<float>();
                if (item.contains("frame")) icon.frame = item.at("frame").get<int>();
                icons.push_back(icon);
                int iconIndex = icons.size() - 1;

                for (auto& id : getStringList(item, "cell")) {
                    auto* form = GetForm<TESObjectCELL>(id);
                    if (!form || cellIcons.contains(form)) continue;
                    cellIcons.insert({form, iconIndex});
                }
                for (auto& id : getStringList(item, "world")) {
                    auto* form = GetForm<TESWorldSpace>(id);
                    if (!form || worldIcons.contains(form)) continue;
                    worldIcons.insert({form, iconIndex});
                }
                for (auto& name : getStringList(item, "name")) {
                    if (name.empty() || data.contains(name)) continue;
                    data.insert({name, iconIndex});
                }
            }
        } catch (...) {
        }
    }
    initalized = true;
}

class MyFunctionHandler : public GFxFunctionHandler {
public:
    virtual void Call(Params& params) override {
        CompileLocalIcons();
        // map localMapMenu markers to the list we compiled
        const auto ui = RE::UI::GetSingleton();
        if (!ui) return;
        const auto menu = ui->GetMenu<MapMenu>();
        if (!menu) return;
        auto markers = menu->GetRuntimeData()->localMapMenu.mapMarkers;
        std::map<int, int> result;
        int i = -1;
        for (auto& marker : markers) {
            i++;
            if (!marker.ref) continue;
            RE::TESObjectREFRPtr refr;
            RE::LookupReferenceByHandle(marker.ref, refr);
            if (auto* ref = refr.get(); ref) {
                auto teleportDoorHandle = ref->extraList.GetTeleportLinkedDoor();
                if (!teleportDoorHandle) continue;
                auto* teleportDoor = teleportDoorHandle.get().get();
                if (auto* cell = teleportDoor->GetParentCell(); cell) {
                    if (cell->IsExteriorCell()) {
                        auto* world = teleportDoor->GetWorldspace();
                        if (worldIcons.contains(world) || (!data.empty() && data.contains(world->GetFullName()))) {
                            result.insert({i, worldIcons.at(world)});
                        }
                    } else {
                        if (cellIcons.contains(cell)) {
                            result.insert({i, cellIcons.at(cell)});
                        } else if (!data.empty()) { // legacy search by marker._label
                            if (auto location = cell->GetLocation(); location) {
                                if (data.contains(location->GetFullName())) {
                                    result.insert({i, data.at(location->GetFullName())});
                                }
                            }
                        }
                    }
                }
            }
        }

        // push the result to uiMovie
        const auto movie = menu->uiMovie;
        GFxValue data;
        movie->CreateArray(&data);
        data.SetArraySize(result.size());
        i = 0;
        for (auto [markerIndex, iconIndex] : result) {
            const auto& icon = icons[iconIndex];
            GFxValue item;
            movie->CreateArray(&item);
            item.SetArraySize(4);
            item.SetElement(0, markerIndex);
            item.SetElement(1, icon.source.c_str());
            item.SetElement(2, icon.scale);
            item.SetElement(3, icon.frame);
            data.SetElement(i, item);
            i++;
        }
        GFxValue root;
        movie->GetVariable(&root, "_root");
        root.SetMember("LMI_data", data);
    }
};

void Inject() {
    const auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    const auto menu = ui->GetMenu<MapMenu>();
    if (!menu) {
        return;
    }

    const auto movie = menu->uiMovie;
    if (!movie) {
        return;
    }

    RE::GFxValue _root;
    movie->GetVariable(&_root, "_root");

    GFxValue function;
    movie->CreateFunction(&function, new MyFunctionHandler());
    _root.SetMember("LMI_Compile", function);

    RE::GFxValue args[2];
    args[0] = RE::GFxValue("LMI");
    args[1] = RE::GFxValue(8654);
    _root.Invoke("createEmptyMovieClip", nullptr, args, 2);
    if (movie->GetVariable(&_root, "_root.LMI")) {
        RE::GFxValue args2[1];
        args2[0] = RE::GFxValue("localmapicons_inject.swf");
        _root.Invoke("loadMovie", nullptr, args2, 1);
    }
}

class MyEvents : public BSTEventSink<MenuOpenCloseEvent> {
    BSEventNotifyControl ProcessEvent(const MenuOpenCloseEvent* event, BSTEventSource<MenuOpenCloseEvent>* override) {
        if (event->menuName != MapMenu::MENU_NAME) return BSEventNotifyControl::kContinue;
        if (event->opening) {
            Inject();
        } else {
            readBuffer.clear();
        }
        return BSEventNotifyControl::kContinue;
    }
};

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        static MyEvents theSink;
        RE::UI::GetSingleton()->AddEventSink(&theSink);
    } else if (message->type == SKSE::MessagingInterface::kNewGame ||
               message->type == SKSE::MessagingInterface::kPostLoadGame) {
        
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    SetupLog();
    SKSE::Init(skse);
    if (std::filesystem::exists("Data/SKSE/Plugins/LocalMapIcons")) {
        SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    }
    return true;
}
