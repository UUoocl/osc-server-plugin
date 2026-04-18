#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

struct OscClient {
    std::string name;
    std::string ip;
    int portOut; // Port to send OSC to device
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

    void AddClient(const OscClient& client);
    void RemoveClient(const std::string& name);
    std::vector<OscClient> GetClients();
    void ClearClients();

    // Send OSC message to all clients on their portOut
    void SendOscMessage(const std::string& address, const char* format, ...);
    void SendOscToClient(const std::string& clientName, const std::string& address, const char* format, ...);
    
    // Support dynamic arguments from Browser Sources
    void SendOscRaw(const std::string& address, const std::string& format, struct obs_data_array* args);

    // Callback for when an OSC message is received
    using OscMessageCallback = std::function<void(const std::string& clientName, const std::string& address, const std::string& jsonArgs)>;
    void SetMessageCallback(OscMessageCallback cb) { messageCallback = cb; }
    
    void LoadConfig();
    
    void SetTargetSource(const std::string& name) { targetSource = name; }
    std::string GetTargetSource() const { return targetSource; }

    using OscLogCallback = std::function<void(const std::string& msg)>;
    void SetLogCallback(OscLogCallback cb) { logCallback = cb; }
    void EnableLogging(bool enable) { loggingEnabled = enable; }
    bool IsLoggingEnabled() const { return loggingEnabled; }

private:
    void ListenerThread();
    void HttpListenerThread();

    std::atomic<bool> running{false};
    std::thread listenerThread;
    std::thread httpThread;
    
    std::string serverIp = "127.0.0.1";
    int serverPort = 12346;
    int httpPort = 12347;
    std::string targetSource = "";
    int serverSocket = -1;
    int httpSocket = -1;

    std::vector<OscClient> clients;
    mutable std::mutex clientsMutex;

    OscMessageCallback messageCallback;
    OscLogCallback logCallback;
    std::atomic<bool> loggingEnabled{false};
};

OscManager& GetOscManager();
