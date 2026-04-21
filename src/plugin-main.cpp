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

// Bridge OSC to Bridge (WebSocket)
void HandleOscToBridge(const std::string &clientName, const std::string &address, const std::string &jsonArgs)
{
	// Build JSON packet for the bridge
	// Format: {"a": "osc_message", "client": "...", "address": "...", "args": [...]}
	std::string finalJson = "{\"a\":\"osc_message\", \"client\":\"" + clientName + "\", \"address\":\"" +
				address + "\", \"args\":" + jsonArgs + "}";

	obs_data_t *packet = obs_data_create_from_json(finalJson.c_str());
	if (packet) {
		signal_handler_t *sh = obs_get_signal_handler();
		calldata_t cd = {0};
		calldata_set_ptr(&cd, "packet", packet);
		signal_handler_signal(sh, "media_warp_transmit", &cd);
		calldata_free(&cd);
		obs_data_release(packet);
	}
}

// Handle Inbound from Bridge
static void on_media_warp_receive(void *data, calldata_t *cd)
{
	(void)data;
	const char *json_str = calldata_string(cd, "json_str");
	if (!json_str)
		return;

	obs_data_t *msg = obs_data_create_from_json(json_str);
	if (!msg)
		return;

	const char *a = obs_data_get_string(msg, "a");
	if (a && strcmp(a, "osc_message") == 0) {
		const char *address = obs_data_get_string(msg, "address");
		const char *format = obs_data_get_string(msg, "format");
		obs_data_array_t *args = obs_data_get_array(msg, "args");
		if (address && format && args) {
			GetOscManager().SendOscRaw(address, format, args);
		}
		obs_data_array_release(args);
	}

	obs_data_release(msg);
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[OSC Server] Plugin loading...");

	auto &mgr = GetOscManager();
	mgr.LoadConfig();
	mgr.SetMessageCallback(HandleOscToBridge);

	if (mgr.GetAutoStart()) {
		mgr.StartServer();
	}

	// Connect to bridge signals
	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_connect(sh, "media_warp_receive", on_media_warp_receive, nullptr);

	obs_frontend_add_event_callback(
		[](enum obs_frontend_event event, void * /*param*/) {
			if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
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

	signal_handler_t *sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "media_warp_receive", on_media_warp_receive, nullptr);

	auto &mgr = GetOscManager();
	mgr.StopServer();
}
