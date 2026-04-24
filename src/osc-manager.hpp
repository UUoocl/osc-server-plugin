#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

#if defined(_WIN32) || defined(_WIN64)
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#endif
#include <winsock2.h>
typedef SOCKET osc_socket_t;
#else
typedef int osc_socket_t;
#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif
#endif

struct OscClient {
	std::string name;
	std::string ip;
	int portOut; // Port to send OSC to device
};

class OscManager {
public:
	OscManager();
	~OscManager();

	void SetServerConfig(const std::string &ip, int port);
	void StartServer();
	void StopServer();
	bool IsServerRunning() const { return running; }
	int GetServerPort() const { return serverPort; }
	std::string GetServerIp() const { return serverIp; }

	void SetAutoStart(bool enable) { autoStart = enable; }
	bool GetAutoStart() const { return autoStart; }

	void SetBroadcastGeneral(bool enable) { broadcastGeneral = enable; }
	bool ShouldBroadcastGeneral() const { return broadcastGeneral; }

	void SetBroadcastByDevice(bool enable) { broadcastByDevice = enable; }
	bool ShouldBroadcastByDevice() const { return broadcastByDevice; }

	void AddClient(const OscClient &client);
	void RemoveClient(const std::string &name);
	std::vector<OscClient> GetClients();
	void ClearClients();

	// Send OSC message to all clients on their portOut
	void SendOscMessage(const std::string &address, const char *format, ...);
	void SendOscToClient(const std::string &clientName, const std::string &address, const char *format, ...);

	// Support dynamic arguments from Browser Sources
	void SendOscRaw(const std::string &address, const std::string &format, struct obs_data_array *args);
	void SendOscToTarget(const std::string &target, const std::string &address, const std::string &format,
			     struct obs_data_array *args);

	// Callback for when an OSC message is received
	using OscMessageCallback =
		std::function<void(const std::string &clientName, const std::string &address, struct obs_data_array *args)>;
	void SetMessageCallback(OscMessageCallback cb) { messageCallback = cb; }

	void LoadConfig();
	void SaveConfig();



	using OscLogCallback = std::function<void(const std::string &msg)>;
	void SetLogCallback(OscLogCallback cb) { logCallback = cb; }
	void EnableLogging(bool enable) { loggingEnabled = enable; }
	bool IsLoggingEnabled() const { return loggingEnabled; }

	void SetLogCollapsed(bool collapsed) { logCollapsed = collapsed; }
	bool IsLogCollapsed() const { return logCollapsed; }

	// Port check helper
	static bool IsPortAvailable(int port);

private:
	void ListenerThread();
	void SendToAddr(const std::string &ip, int port, const char *buffer, uint32_t len);

	std::atomic<bool> running{false};
	std::thread listenerThread;

	std::string serverIp = "127.0.0.1";
	int serverPort = 12346;
	osc_socket_t serverSocket = INVALID_SOCKET;



	std::vector<OscClient> clients;
	mutable std::mutex clientsMutex;

	OscMessageCallback messageCallback;
	OscLogCallback logCallback;
	std::atomic<bool> loggingEnabled{false};
	std::atomic<bool> autoStart{false};
	std::atomic<bool> logCollapsed{false};
	std::atomic<bool> broadcastGeneral{true};
	std::atomic<bool> broadcastByDevice{true};
};

OscManager &GetOscManager();
