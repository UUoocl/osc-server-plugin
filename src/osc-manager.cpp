#include "osc-manager.hpp"
#include "thirdparty/tinyosc.h"
#include "thirdparty/mongoose.h"
#include <obs-module.h>
#include <obs.h>
#include <util/platform.h>

#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>

#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// Mongoose event handler
static void mongoose_fn(struct mg_connection *c, int ev, void *ev_data)
{
	if (ev == MG_EV_HTTP_MSG) {
		struct mg_http_message *hm = (struct mg_http_message *)ev_data;
		if (mg_match(hm->uri, mg_str("/ws"), NULL)) {
			mg_ws_upgrade(c, hm, NULL);
		} else if (mg_match(hm->uri, mg_str("/health"), NULL)) {
			mg_http_reply(c, 200, "", "ok");
		}
	} else if (ev == MG_EV_WS_MSG) {
		struct mg_ws_message *wm = (struct mg_ws_message *)ev_data;
		std::string msg(wm->data.buf, wm->data.len);

		obs_data_t *data = obs_data_create_from_json(msg.c_str());
		if (data) {
			const char *address = obs_data_get_string(data, "address");
			const char *format = obs_data_get_string(data, "format");
			obs_data_array_t *args = obs_data_get_array(data, "args");
			const char *target = obs_data_get_string(data, "target");

			if (address && format) {
				if (target && *target) {
					GetOscManager().SendOscToTarget(target, address, format, args);
				} else {
					GetOscManager().SendOscRaw(address, format, args);
				}
			}

			obs_data_array_release(args);
			obs_data_release(data);
		}
	}
}

OscManager::OscManager()
{
#if defined(_WIN32) || defined(_WIN64)
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

OscManager::~OscManager()
{
	StopServer();
	StopMongoose();
#if defined(_WIN32) || defined(_WIN64)
	WSACleanup();
#endif
}

void OscManager::SetServerConfig(const std::string &ip, int port)
{
	if (running) {
		StopServer();
		serverIp = ip;
		serverPort = port;
		StartServer();
	} else {
		serverIp = ip;
		serverPort = port;
	}
}

void OscManager::StartServer()
{
	if (running)
		return;

	serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (serverSocket == INVALID_SOCKET) {
		blog(LOG_ERROR, "[OSC Server] Failed to create socket");
		return;
	}

	struct sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(serverPort);
	serverAddr.sin_addr.s_addr = inet_addr(serverIp.c_str());

	if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		blog(LOG_WARNING, "[OSC Server] Failed to bind to %s:%d (Port may be in use)", serverIp.c_str(),
		     serverPort);
#if defined(_WIN32) || defined(_WIN64)
		closesocket(serverSocket);
#else
		close(serverSocket);
#endif
		serverSocket = INVALID_SOCKET;
		return;
	}

	running = true;
	listenerThread = std::thread(&OscManager::ListenerThread, this);
	blog(LOG_INFO, "[OSC Server] Started on %s:%d", serverIp.c_str(), serverPort);
}

void OscManager::StopServer()
{
	if (!running)
		return;
	running = false;

	if (serverSocket != INVALID_SOCKET) {
#if defined(_WIN32) || defined(_WIN64)
		closesocket(serverSocket);
#else
		close(serverSocket);
#endif
		serverSocket = INVALID_SOCKET;
	}

	if (listenerThread.joinable())
		listenerThread.join();
	blog(LOG_INFO, "[OSC Server] Stopped");
}

void OscManager::StartMongoose(int port)
{
	if (mongooseRunning)
		StopMongoose();

	mongoosePort = port;
	mg_mgr_init(&mgr);

	char url[64];
	snprintf(url, sizeof(url), "http://127.0.0.1:%d", port);

	mg_conn = mg_http_listen(&mgr, url, mongoose_fn, NULL);
	if (!mg_conn) {
		blog(LOG_ERROR, "[OSC Server] Mongoose failed to listen on %s", url);
		mg_mgr_free(&mgr);
		return;
	}

	mongooseRunning = true;
	mongooseThread = std::thread(&OscManager::MongooseThread, this);
	blog(LOG_INFO, "[OSC Server] Mongoose started on %s", url);
}

void OscManager::StopMongoose()
{
	if (!mongooseRunning)
		return;
	mongooseRunning = false;

	if (mongooseThread.joinable())
		mongooseThread.join();

	mg_mgr_free(&mgr);
	blog(LOG_INFO, "[OSC Server] Mongoose stopped");
}

void OscManager::MongooseThread()
{
	while (mongooseRunning) {
		mg_mgr_poll(&mgr, 100);
	}
}

bool OscManager::IsPortAvailable(int port)
{
	osc_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == INVALID_SOCKET)
		return false;

	sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	int result = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
#if defined(_WIN32) || defined(_WIN64)
	closesocket(sock);
#else
	close(sock);
#endif
	return result == 0;
}

void OscManager::AddClient(const OscClient &client)
{
	std::lock_guard<std::mutex> lock(clientsMutex);
	clients.push_back(client);
}

void OscManager::RemoveClient(const std::string &name)
{
	std::lock_guard<std::mutex> lock(clientsMutex);
	for (auto it = clients.begin(); it != clients.end(); ++it) {
		if (it->name == name) {
			clients.erase(it);
			break;
		}
	}
}

std::vector<OscClient> OscManager::GetClients()
{
	std::lock_guard<std::mutex> lock(clientsMutex);
	return clients;
}

void OscManager::ClearClients()
{
	std::lock_guard<std::mutex> lock(clientsMutex);
	clients.clear();
}

void OscManager::ListenerThread()
{
	char buffer[4096];
	struct sockaddr_in clientAddr;
#if defined(_WIN32) || defined(_WIN64)
	int addrLen = sizeof(clientAddr);
#else
	socklen_t addrLen = sizeof(clientAddr);
#endif

	while (running) {
		int bytesRead =
			recvfrom(serverSocket, buffer, sizeof(buffer), 0, (struct sockaddr *)&clientAddr, &addrLen);
		if (bytesRead <= 0) {
			continue;
		}

		std::string clientName = "";
		std::string clientTarget = "All Browser Sources";
		char ipStr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
		int port = ntohs(clientAddr.sin_port);

		{
			std::lock_guard<std::mutex> lock(clientsMutex);
			for (const auto &client : clients) {
				if (client.ip == ipStr && client.portOut == port) {
					clientName = client.name;
					clientTarget = client.targetSource;
					break;
				}
			}
		}

		if (clientName.empty()) {
			clientName = "Unknown (" + std::string(ipStr) + ":" + std::to_string(port) + ")";
		}

		if (tosc_isBundle(buffer)) {
			tosc_bundle bundle;
			tosc_parseBundle(&bundle, buffer, bytesRead);
			tosc_message msg;
			while (tosc_getNextMessage(&bundle, &msg)) {
				std::string addr = tosc_getAddress(&msg);
				std::string jsonArgs = "[";
				tosc_reset(&msg);
				char *fmt = tosc_getFormat(&msg);
				for (int i = 0; fmt[i] != '\0'; ++i) {
					if (i > 0)
						jsonArgs += ",";
					switch (fmt[i]) {
					case 'i':
						jsonArgs += std::to_string(tosc_getNextInt32(&msg));
						break;
					case 'f':
						jsonArgs += std::to_string(tosc_getNextFloat(&msg));
						break;
					case 's':
						jsonArgs += "\"" + std::string(tosc_getNextString(&msg)) + "\"";
						break;
					}
				}
				jsonArgs += "]";

				if (messageCallback) {
					messageCallback(clientName, addr, jsonArgs, clientTarget);
				}

				if (loggingEnabled && logCallback) {
					std::string logMsg =
						"[" + clientName + "] " + addr + " " + jsonArgs + " -> " + clientTarget;
					logCallback(logMsg);
				}
			}
		} else {
			tosc_message msg;
			if (tosc_parseMessage(&msg, buffer, bytesRead) == 0) {
				std::string addr = tosc_getAddress(&msg);
				std::string jsonArgs = "[";
				char *fmt = tosc_getFormat(&msg);
				for (int i = 0; fmt[i] != '\0'; ++i) {
					if (i > 0)
						jsonArgs += ",";
					switch (fmt[i]) {
					case 'i':
						jsonArgs += std::to_string(tosc_getNextInt32(&msg));
						break;
					case 'f':
						jsonArgs += std::to_string(tosc_getNextFloat(&msg));
						break;
					case 's':
						jsonArgs += "\"" + std::string(tosc_getNextString(&msg)) + "\"";
						break;
					}
				}
				jsonArgs += "]";

				if (messageCallback) {
					messageCallback(clientName, addr, jsonArgs, clientTarget);
				}

				if (loggingEnabled && logCallback) {
					std::string logMsg =
						"[" + clientName + "] " + addr + " " + jsonArgs + " -> " + clientTarget;
					logCallback(logMsg);
				}
			}
		}
	}
}

void OscManager::SendToAddr(const std::string &ip, int port, const char *buffer, uint32_t len)
{
	osc_socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == INVALID_SOCKET)
		return;

	struct sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(port);
	destAddr.sin_addr.s_addr = inet_addr(ip.c_str());

	sendto(sock, buffer, len, 0, (struct sockaddr *)&destAddr, sizeof(destAddr));
#if defined(_WIN32) || defined(_WIN64)
	closesocket(sock);
#else
	close(sock);
#endif
}

void OscManager::SendOscMessage(const std::string &address, const char *format, ...)
{
	va_list ap;
	char buffer[4096];

	va_start(ap, format);
	uint32_t len = tosc_vwriteMessage(buffer, sizeof(buffer), address.c_str(), format, ap);
	va_end(ap);

	std::lock_guard<std::mutex> lock(clientsMutex);
	for (const auto &client : clients) {
		if (client.portOut > 0) {
			SendToAddr(client.ip, client.portOut, buffer, len);
		}
	}
}

void OscManager::SendOscToClient(const std::string &clientName, const std::string &address, const char *format, ...)
{
	va_list ap;
	char buffer[4096];

	va_start(ap, format);
	uint32_t len = tosc_vwriteMessage(buffer, sizeof(buffer), address.c_str(), format, ap);
	va_end(ap);

	std::lock_guard<std::mutex> lock(clientsMutex);
	for (const auto &client : clients) {
		if (client.name == clientName && client.portOut > 0) {
			SendToAddr(client.ip, client.portOut, buffer, len);
			break;
		}
	}
}

static uint32_t PrepareRawBuffer(char *buffer, size_t bufSize, const std::string &address, const std::string &format,
				 struct obs_data_array *args)
{
	uint32_t i = (uint32_t)address.length();
	if (i >= (uint32_t)bufSize)
		return 0;
	strcpy(buffer, address.c_str());
	i = (i + 4) & ~0x3;

	buffer[i++] = ',';
	int s_len = (int)format.length();
	if (i + (uint32_t)s_len >= (uint32_t)bufSize)
		return 0;
	strcpy(buffer + i, format.c_str());
	i = (i + 4 + (uint32_t)s_len) & ~0x3;

	size_t count = obs_data_array_count(args);
	for (int j = 0; j < (int)format.length() && (size_t)j < count; ++j) {
		obs_data_t *item = obs_data_array_item(args, j);
		if (!item)
			continue;

		switch (format[j]) {
		case 'i': {
			if (i + 4 > (uint32_t)bufSize)
				break;
			int32_t val = (int32_t)obs_data_get_int(item, "value");
			*((uint32_t *)(buffer + i)) = (uint32_t)htonl(val);
			i += 4;
			break;
		}
		case 'f': {
			if (i + 4 > (uint32_t)bufSize)
				break;
			float val = (float)obs_data_get_double(item, "value");
			uint32_t uval;
			memcpy(&uval, &val, 4);
			*((uint32_t *)(buffer + i)) = (uint32_t)htonl(uval);
			i += 4;
			break;
		}
		case 's': {
			const char *s = obs_data_get_string(item, "value");
			int slen = (int)strlen(s);
			if (i + (uint32_t)slen >= (uint32_t)bufSize)
				break;
			strcpy(buffer + i, s);
			i = (i + (uint32_t)slen + 4) & ~0x3;
			break;
		}
		}
		obs_data_release(item);
	}
	return i;
}

void OscManager::SendOscRaw(const std::string &address, const std::string &format, struct obs_data_array *args)
{
	char buffer[4096];
	uint32_t len = PrepareRawBuffer(buffer, sizeof(buffer), address, format, args);
	if (len == 0)
		return;

	std::lock_guard<std::mutex> lock(clientsMutex);
	for (const auto &client : clients) {
		if (client.portOut > 0) {
			SendToAddr(client.ip, client.portOut, buffer, len);
		}
	}
}

void OscManager::SendOscToTarget(const std::string &target, const std::string &address, const std::string &format,
				 struct obs_data_array *args)
{
	char buffer[4096];
	uint32_t len = PrepareRawBuffer(buffer, sizeof(buffer), address, format, args);
	if (len == 0)
		return;

	int targetPort = 0;
	try {
		targetPort = std::stoi(target);
	} catch (...) {
	}

	std::lock_guard<std::mutex> lock(clientsMutex);
	for (const auto &client : clients) {
		bool match = false;
		if (targetPort > 0 && client.portOut == targetPort)
			match = true;
		else if (client.name == target)
			match = true;

		if (match) {
			SendToAddr(client.ip, client.portOut, buffer, len);
		}
	}
}

void OscManager::LoadConfig()
{
	char *configPath = obs_module_config_path("osc-server-settings.json");
	if (!configPath)
		return;

	obs_data_t *data = obs_data_create_from_json_file(configPath);
	bfree(configPath);

	if (data) {
		const char *ip = obs_data_get_string(data, "server_ip");
		int port = (int)obs_data_get_int(data, "server_port");
		int mPort = (int)obs_data_get_int(data, "mongoose_port");
		const char *target = obs_data_get_string(data, "target_source");

		if (ip && *ip)
			serverIp = ip;
		if (port > 0)
			serverPort = port;
		if (mPort > 0)
			mongoosePort = mPort;
		if (target)
			targetSource = target;
		autoStart = obs_data_get_bool(data, "auto_start");
		logCollapsed = obs_data_get_bool(data, "log_collapsed");

		std::lock_guard<std::mutex> lock(clientsMutex);
		clients.clear();

		obs_data_array_t *clientsArray = obs_data_get_array(data, "clients");
		if (clientsArray) {
			size_t count = obs_data_array_count(clientsArray);
			for (size_t i = 0; i < count; i++) {
				obs_data_t *clientData = obs_data_array_item(clientsArray, i);
				OscClient client;
				client.name = obs_data_get_string(clientData, "name");
				client.ip = obs_data_get_string(clientData, "ip");
				client.portOut = (int)obs_data_get_int(clientData, "portOut");
				client.targetSource = obs_data_get_string(clientData, "targetSource");
				if (client.targetSource.empty())
					client.targetSource = "All Browser Sources";
				clients.push_back(client);
				obs_data_release(clientData);
			}
			obs_data_array_release(clientsArray);
		}
		obs_data_release(data);
	}
}

void OscManager::SaveConfig()
{
	char *configPath = obs_module_config_path("osc-server-settings.json");
	if (!configPath)
		return;

	// Ensure directory exists
	std::string path(configPath);
	size_t lastSlash = path.find_last_of("/\\");
	if (lastSlash != std::string::npos) {
		std::string dir = path.substr(0, lastSlash);
		os_mkdir(dir.c_str());
	}

	obs_data_t *data = obs_data_create();
	obs_data_set_string(data, "server_ip", serverIp.c_str());
	obs_data_set_int(data, "server_port", serverPort);
	obs_data_set_int(data, "mongoose_port", mongoosePort);
	obs_data_set_string(data, "target_source", targetSource.c_str());
	obs_data_set_bool(data, "auto_start", autoStart);
	obs_data_set_bool(data, "log_collapsed", logCollapsed);

	obs_data_array_t *clientsArray = obs_data_array_create();
	std::lock_guard<std::mutex> lock(clientsMutex);
	for (const auto &client : clients) {
		obs_data_t *clientData = obs_data_create();
		obs_data_set_string(clientData, "name", client.name.c_str());
		obs_data_set_string(clientData, "ip", client.ip.c_str());
		obs_data_set_int(clientData, "portOut", client.portOut);
		obs_data_set_string(clientData, "targetSource", client.targetSource.c_str());
		obs_data_array_push_back(clientsArray, clientData);
		obs_data_release(clientData);
	}
	obs_data_set_array(data, "clients", clientsArray);
	obs_data_array_release(clientsArray);

	obs_data_save_json(data, configPath);
	obs_data_release(data);
	bfree(configPath);
}

OscManager &GetOscManager()
{
	static OscManager instance;
	return instance;
}
