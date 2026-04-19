#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include "thirdparty/mongoose.h"

struct OscClient {
    std::string name;
    std::string ip;
    int portOut; // Port to send OSC to device
    std::string targetSource = "All Browser Sources"; // Target browser source for this device
};

class OscManager {
public:
    OscManager();
    ~OscManager();

    void SetServerConfig(const std::string& ip, int port);
    void StartServer();
    void StopServer();
    bool IsServerRunning() const { return running; }
    int GetServerPort() const { return serverPort; }
    std::string GetServerIp() const { return serverIp; }

    // Mongoose Webserver control
    void StartMongoose(int port);
    void StopMongoose();
    bool IsMongooseRunning() const { return mongooseRunning; }
    int GetMongoosePort() const { return mongoosePort; }
    void SetMongoosePort(int port) { mongoosePort = port; }
    
    void SetAutoStart(bool enable) { autoStart = enable; }
    bool GetAutoStart() const { return autoStart; }

    void AddClient(const OscClient& client);
    void RemoveClient(const std::string& name);
    std::vector<OscClient> GetClients();
    void ClearClients();

    // Send OSC message to all clients on their portOut
    void SendOscMessage(const std::string& address, const char* format, ...);
    void SendOscToClient(const std::string& clientName, const std::string& address, const char* format, ...);
    
    // Support dynamic arguments from Browser Sources
    void SendOscRaw(const std::string& address, const std::string& format, struct obs_data_array* args);
    void SendOscToTarget(const std::string& target, const std::string& address, const std::string& format, struct obs_data_array* args);

    // Callback for when an OSC message is received
    using OscMessageCallback = std::function<void(const std::string& clientName, const std::string& address, const std::string& jsonArgs, const std::string& target)>;
    void SetMessageCallback(OscMessageCallback cb) { messageCallback = cb; }
    
    void LoadConfig();
    void SaveConfig();
    
    void SetTargetSource(const std::string& name) { targetSource = name; }
    std::string GetTargetSource() const { return targetSource; }

    using OscLogCallback = std::function<void(const std::string& msg)>;
    void SetLogCallback(OscLogCallback cb) { logCallback = cb; }
    void EnableLogging(bool enable) { loggingEnabled = enable; }
    bool IsLoggingEnabled() const { return loggingEnabled; }

    void SetLogCollapsed(bool collapsed) { logCollapsed = collapsed; }
    bool IsLogCollapsed() const { return logCollapsed; }

    // Port check helper
    static bool IsPortAvailable(int port);

private:
    void ListenerThread();
    void MongooseThread();
    void SendToAddr(const std::string& ip, int port, const char* buffer, uint32_t len);

    std::atomic<bool> running{false};
    std::thread listenerThread;
    
    std::string serverIp = "127.0.0.1";
    int serverPort = 12346;
    int serverSocket = -1;

    // Mongoose members
    std::atomic<bool> mongooseRunning{false};
    std::atomic<int> mongoosePort{12347};
    std::thread mongooseThread;
    struct mg_mgr mgr;
    struct mg_connection* mg_conn = nullptr;

    std::string targetSource = "";

    std::vector<OscClient> clients;
    mutable std::mutex clientsMutex;

    OscMessageCallback messageCallback;
    OscLogCallback logCallback;
    std::atomic<bool> loggingEnabled{false};
    std::atomic<bool> autoStart{false};
    std::atomic<bool> logCollapsed{false};
};

OscManager& GetOscManager();
