#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QAction>

#include "osc-manager.hpp"
#include "osc-settings-dialog.hpp"
#include "thirdparty/tinyosc.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("osc-server-plugin", "en-US")

static OscSettingsDialog* settingsDialog = nullptr;

// Bridge OSC to Browser Source
void HandleOscToBrowser(const std::string& clientName, const std::string& address, const std::string& jsonArgs) {
    struct Params {
        const std::string& clientName;
        const std::string& address;
        const std::string& jsonArgs;
        std::string target;
    } params = { clientName, address, jsonArgs, GetOscManager().GetTargetSource() };

    obs_enum_sources([](void* data, obs_source_t* source) {
        Params* p = (Params*)data;
        const char* id = obs_source_get_id(source);
        if (id && strcmp(id, "browser_source") == 0) {
            const char* name = obs_source_get_name(source);
            
            // Filter by target source name if specified
            if (p->target != "All Browser Sources" && !p->target.empty()) {
                if (p->target != name) return true;
            }

            proc_handler_t* ph = obs_source_get_proc_handler(source);
            if (ph) {
                calldata_t cd;
                calldata_init(&cd);
                calldata_set_string(&cd, "eventName", "osc_message");
                
                std::string finalJson = "{\"client\":\"" + p->clientName + "\", \"address\":\"" + p->address + "\", \"args\":" + p->jsonArgs + "}";
                
                calldata_set_string(&cd, "jsonString", finalJson.c_str());
                proc_handler_call(ph, "javascript_event", &cd);
                calldata_free(&cd);
            }
        }
        return true;
    }, &params);
}

// Bridge Browser Source to OSC
static void BrowserToOscCallback(void* /*data*/, calldata_t* cd) {
    const char* address = calldata_string(cd, "address");
    const char* format = calldata_string(cd, "format");
    const char* argsJson = calldata_string(cd, "args");

    if (!address || !format) return;

    if (!argsJson || strlen(argsJson) == 0 || strcmp(argsJson, "[]") == 0) {
        GetOscManager().SendOscMessage(address, format);
        return;
    }

    // Use obs_data to parse the JSON array
    // Since obs_data doesn't support arrays at the root easily, 
    // we wrap it in an object: {"args": ...}
    std::string wrapped = "{\"args\":" + std::string(argsJson) + "}";
    obs_data_t* data = obs_data_create_from_json(wrapped.c_str());
    if (!data) {
        GetOscManager().SendOscMessage(address, format);
        return;
    }

    obs_data_array_t* array = obs_data_get_array(data, "args");
    if (!array) {
        obs_data_release(data);
        GetOscManager().SendOscMessage(address, format);
        return;
    }

    GetOscManager().SendOscRaw(address, format, array);

    obs_data_array_release(array);
    obs_data_release(data);
}

static bool RegisterSourceProcs(void* /*data*/, obs_source_t* source) {
    const char* name = obs_source_get_name(source);
    const char* id = obs_source_get_id(source);
    
    // Log EVERY source to help find the right ID
    blog(LOG_INFO, "[OSC Server] Scanning source: name='%s', id='%s'", name, id);

    if (id && (strcmp(id, "browser_source") == 0 || strstr(id, "browser") != nullptr)) {
        blog(LOG_INFO, "[OSC Server] Identified '%s' as a browser source. Attaching procedures...", name);
        proc_handler_t* ph = obs_source_get_proc_handler(source);
        if (ph) {
            proc_handler_add(ph, "void send_osc(string address, string format, string args)", BrowserToOscCallback, nullptr);
            blog(LOG_INFO, "[OSC Server] 'send_osc' attached to '%s'", name);
        }
    }
    return true;
}

bool obs_module_load(void) {
    blog(LOG_INFO, "[OSC Server] Plugin loading...");

    // Register on global handler too
    proc_handler_t* globalPh = obs_get_proc_handler();
    if (globalPh) {
        proc_handler_add(globalPh, "void send_osc(string address, string format, string args)", BrowserToOscCallback, nullptr);
    }

    auto& mgr = GetOscManager();
    mgr.LoadConfig();
    mgr.SetMessageCallback(HandleOscToBrowser);

    // Register proc for future sources using signal handler
    signal_handler_t* sh = obs_get_signal_handler();
    signal_handler_connect(sh, "source_create", [](void* /*data*/, calldata_t* cd) {
        obs_source_t* source = (obs_source_t*)calldata_ptr(cd, "source");
        RegisterSourceProcs(nullptr, source);
    }, nullptr);

    obs_frontend_add_event_callback([](enum obs_frontend_event event, void* /*param*/) {
        if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
             blog(LOG_INFO, "[OSC Server] OBS loading finished, registering source procedures...");
             
             // Register proc on existing sources
             obs_enum_sources(RegisterSourceProcs, nullptr);

             QAction* action = (QAction*)obs_frontend_add_tools_menu_qaction("OSC Server Settings");
             QObject::connect(action, &QAction::triggered, []() {
                if (!settingsDialog) {
                    settingsDialog = new OscSettingsDialog((QWidget*)obs_frontend_get_main_window());
                }
                settingsDialog->show();
                settingsDialog->raise();
                settingsDialog->activateWindow();
            });
        }
    }, nullptr);

    return true;
}

void obs_module_unload(void) {
    if (settingsDialog) {
        delete settingsDialog;
        settingsDialog = nullptr;
    }
    GetOscManager().StopServer();
}
