#include "logger.h"
#include "nlohmann/json.hpp";

using namespace RE;
using JSON = nlohmann::json;

struct Icon {
    std::string source;
    int frame = 1;
    float scale = 1.0;
};

static bool initalized = false;
static std::string readBuffer;
std::unordered_map<std::string, Icon> data;

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
    if (initalized) return;
    const std::filesystem::path dir = "Data/SKSE/Plugins/LocalMapIcons";
    for (auto& file : std::filesystem::directory_iterator(dir)) {
        auto ext = file.path().extension().string();
        std::ranges::transform(ext, ext.begin(), ::tolower);
        if (ext != ".json") continue;
        std::ifstream ifile{file.path()};
        if (!ifile) continue;
        try {
            JSON json = JSON::parse(ifile);
            if (json.is_discarded()) continue;
            for (const auto& item : json) {
                if (!item.contains("icon")) continue;
                Icon icon{};

                auto names = getStringList(item, "name");
                auto cells = getStringList(item, "cell");
                auto worlds = getStringList(item, "world");

                // both "name" and "cell" can be either string, or array of strings
                for (auto& id : cells) {
                    auto* form = GetForm<TESObjectCELL>(id);
                    if (!form) continue;
                    names.push_back(form->fullName.c_str());
                }
                for (auto& id : worlds) {
                    auto* form = GetForm<TESWorldSpace>(id);
                    if (!form) continue;
                    names.push_back(form->fullName.c_str());
                }

                if (names.empty()) continue;

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

                for (auto& name : names) {
                    data.emplace(name, icon);
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
        if (params.argCount > 0 && params.args[0].IsString()) {
            std::string key = params.args[0].GetString();
            auto it = data.find(key);
            if (it != data.end()) {
                readBuffer.clear();
                readBuffer =
                    it->second.source + "," + std::to_string(it->second.scale) + "," + std::to_string(it->second.frame);
                params.retVal->SetString(readBuffer.c_str());
            }
        }
    }
};

void Inject(std::string_view a_menuName) {
    const auto ui = RE::UI::GetSingleton();
    if (!ui) return;

    const auto menu = ui->GetMenu(a_menuName);
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
    _root.SetMember("GetLocalMarkers", function);

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
        if (event->opening && event->menuName == MapMenu::MENU_NAME) {
            Inject(MapMenu::MENU_NAME);
        }
        return BSEventNotifyControl::kContinue;
    }
};

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        static MyEvents theSink;
        RE::UI::GetSingleton()->AddEventSink(&theSink);
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
