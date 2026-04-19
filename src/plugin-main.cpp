#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QAction>
#include <QPointer>
#include <QTimer>

#include "osc-manager.hpp"
#include "osc-settings-dialog.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("osc-server", "en-US")

static QPointer<OscSettingsDialog> settingsDialog;

// Emit server details to all browser sources
void EmitServerDetails()
{
	auto &mgr = GetOscManager();
	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "ip", "127.0.0.1");
	obs_data_set_int(data, "port", mgr.GetMongoosePort());

	obs_data_array_t *clientsArr = obs_data_array_create();
	for (const auto &client : mgr.GetClients()) {
		obs_data_t *c = obs_data_create();
		obs_data_set_string(c, "name", client.name.c_str());
		obs_data_set_int(c, "portOut", client.portOut);
		obs_data_array_push_back(clientsArr, c);
		obs_data_release(c);
	}
	obs_data_set_array(data, "clients", clientsArr);
	obs_data_array_release(clientsArr);

	const char *json = obs_data_get_json(data);

	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			const char *jsonStr = (const char *)data;
			const char *id = obs_source_get_id(source);
			if (id && strcmp(id, "browser_source") == 0) {
				proc_handler_t *ph = obs_source_get_proc_handler(source);
				if (ph) {
					calldata_t cd;
					calldata_init(&cd);
					calldata_set_string(&cd, "eventName", "osc_server_details");
					calldata_set_string(&cd, "jsonString", jsonStr);
					proc_handler_call(ph, "javascript_event", &cd);
					calldata_free(&cd);
				}
			}
			return true;
		},
		(void *)json);

	obs_data_release(data);
}

// Bridge OSC to Browser Source
void HandleOscToBrowser(const std::string &clientName, const std::string &address, const std::string &jsonArgs,
			const std::string &target)
{
	struct Params {
		const std::string &clientName;
		const std::string &address;
		const std::string &jsonArgs;
		std::string target;
	} params = {clientName, address, jsonArgs, target};

	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			Params *p = (Params *)data;
			const char *id = obs_source_get_id(source);
			if (id && strcmp(id, "browser_source") == 0) {
				const char *name = obs_source_get_name(source);

				if (p->target != "All Browser Sources" && !p->target.empty()) {
					if (p->target != name)
						return true;
				}

				proc_handler_t *ph = obs_source_get_proc_handler(source);
				if (ph) {
					calldata_t cd;
					calldata_init(&cd);
					calldata_set_string(&cd, "eventName", "osc_message");
					std::string finalJson = "{\"client\":\"" + p->clientName +
								"\", \"address\":\"" + p->address +
								"\", \"args\":" + p->jsonArgs + "}";
					calldata_set_string(&cd, "jsonString", finalJson.c_str());
					proc_handler_call(ph, "javascript_event", &cd);
					calldata_free(&cd);
				}
			}
			return true;
		},
		&params);
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[OSC Server] Plugin loading...");

	auto &mgr = GetOscManager();
	mgr.LoadConfig();
	mgr.SetMessageCallback(HandleOscToBrowser);

	if (mgr.GetAutoStart()) {
		mgr.StartServer();
		mgr.StartMongoose(mgr.GetMongoosePort());
	}

	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void * /*param*/) {
			if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
				// Emit details after loading with a 5s delay
				QTimer::singleShot(5000, []() { EmitServerDetails(); });

				QAction *action = (QAction *)obs_frontend_add_tools_menu_qaction("OSC Server Settings");
				QObject::connect(action, &QAction::triggered, []() {
					if (!settingsDialog) {
						settingsDialog = new OscSettingsDialog(
							(QWidget *)obs_frontend_get_main_window());
						settingsDialog->setAttribute(Qt::WA_DeleteOnClose);
					}
					settingsDialog->show();
					settingsDialog->raise();
					settingsDialog->activateWindow();
				});
			}
		},
		nullptr);

	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[OSC Server] Unloading...");

	if (settingsDialog) {
		delete settingsDialog;
	}

	auto &mgr = GetOscManager();
	mgr.StopServer();
	mgr.StopMongoose();
}
