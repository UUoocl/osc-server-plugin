#include "osc-manager.hpp"
#include "thirdparty/tinyosc.h"
#include <obs-module.h>
#include <obs.h>

#include <iostream>
#include <sstream>

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

OscManager::OscManager() {
#if defined(_WIN32) || defined(_WIN64)
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

OscManager::~OscManager() {
    StopServer();
#if defined(_WIN32) || defined(_WIN64)
    WSACleanup();
#endif
}

void OscManager::SetServerConfig(const std::string& ip, int port) {
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

void OscManager::StartServer() {
    if (running) return;

    serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        blog(LOG_ERROR, "[OSC Server] Failed to create socket");
        return;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    serverAddr.sin_addr.s_addr = inet_addr(serverIp.c_str());

    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        blog(LOG_WARNING, "[OSC Server] Failed to bind to %s:%d (Port may be in use by another script)", serverIp.c_str(), serverPort);
#if defined(_WIN32) || defined(_WIN64)
        closesocket(serverSocket);
#else
        close(serverSocket);
#endif
        serverSocket = -1;
        return;
    }

    running = true;
    listenerThread = std::thread(&OscManager::ListenerThread, this);

    // Start HTTP Bridge
    httpSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (httpSocket != INVALID_SOCKET) {
        int opt = 1;
        setsockopt(httpSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
        
        sockaddr_in httpAddr;
        httpAddr.sin_family = AF_INET;
        httpAddr.sin_addr.s_addr = inet_addr(serverIp.c_str());
        httpAddr.sin_port = htons(httpPort);
        
        if (bind(httpSocket, (struct sockaddr*)&httpAddr, sizeof(httpAddr)) != SOCKET_ERROR) {
            listen(httpSocket, 5);
            httpThread = std::thread(&OscManager::HttpListenerThread, this);
            blog(LOG_INFO, "[OSC Server] HTTP Bridge started on %s:%d", serverIp.c_str(), httpPort);
        } else {
            blog(LOG_ERROR, "[OSC Server] Failed to bind HTTP Bridge to port %d", httpPort);
            close(httpSocket);
            httpSocket = INVALID_SOCKET;
        }
    }
    blog(LOG_INFO, "[OSC Server] Started on %s:%d", serverIp.c_str(), serverPort);
}

void OscManager::StopServer() {
    if (!running) return;
    running = false;

    if (serverSocket != INVALID_SOCKET) {
#if defined(_WIN32) || defined(_WIN64)
        closesocket(serverSocket);
#else
        close(serverSocket);
#endif
        serverSocket = INVALID_SOCKET;
    }

    if (httpSocket != INVALID_SOCKET) {
#if defined(_WIN32) || defined(_WIN64)
        closesocket(httpSocket);
#else
        close(httpSocket);
#endif
        httpSocket = INVALID_SOCKET;
    }

    if (listenerThread.joinable()) listenerThread.join();
    if (httpThread.joinable()) httpThread.join();
    blog(LOG_INFO, "[OSC Server] Stopped");
}

void OscManager::AddClient(const OscClient& client) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    clients.push_back(client);
}

void OscManager::RemoveClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->name == name) {
            clients.erase(it);
            break;
        }
    }
}

std::vector<OscClient> OscManager::GetClients() {
    std::lock_guard<std::mutex> lock(clientsMutex);
    return clients;
}

void OscManager::ClearClients() {
    std::lock_guard<std::mutex> lock(clientsMutex);
    clients.clear();
}

void OscManager::ListenerThread() {
    char buffer[4096];
    struct sockaddr_in clientAddr;
#if defined(_WIN32) || defined(_WIN64)
    int addrLen = sizeof(clientAddr);
#else
    socklen_t addrLen = sizeof(clientAddr);
#endif

    while (running) {
        int bytesRead = recvfrom(serverSocket, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientAddr, &addrLen);
        if (bytesRead <= 0) {
            continue;
        }

        // Identify client
        std::string clientName = "";
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
        int port = ntohs(clientAddr.sin_port);

        {
            std::lock_guard<std::mutex> lock(clientsMutex);
            for (const auto& client : clients) {
                if (client.ip == ipStr && client.portOut == port) {
                    clientName = client.name;
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
                    if (i > 0) jsonArgs += ",";
                    switch (fmt[i]) {
                        case 'i': jsonArgs += std::to_string(tosc_getNextInt32(&msg)); break;
                        case 'f': jsonArgs += std::to_string(tosc_getNextFloat(&msg)); break;
                        case 's': jsonArgs += "\"" + std::string(tosc_getNextString(&msg)) + "\""; break;
                    }
                }
                jsonArgs += "]";
                
                if (messageCallback) {
                    messageCallback(clientName, addr, jsonArgs);
                }

                if (loggingEnabled && logCallback) {
                    std::string logMsg = "[" + clientName + "] " + addr + " " + jsonArgs;
                    logCallback(logMsg);
                }

                // Add system log to confirm this plugin received it
                blog(LOG_INFO, "[OSC Server] Received: %s from %s", addr.c_str(), clientName.c_str());
            }
        } else {
            tosc_message msg;
            if (tosc_parseMessage(&msg, buffer, bytesRead) == 0) {
                std::string addr = tosc_getAddress(&msg);
                std::string jsonArgs = "[";
                char *fmt = tosc_getFormat(&msg);
                for (int i = 0; fmt[i] != '\0'; ++i) {
                    if (i > 0) jsonArgs += ",";
                    switch (fmt[i]) {
                        case 'i': jsonArgs += std::to_string(tosc_getNextInt32(&msg)); break;
                        case 'f': jsonArgs += std::to_string(tosc_getNextFloat(&msg)); break;
                        case 's': jsonArgs += "\"" + std::string(tosc_getNextString(&msg)) + "\""; break;
                    }
                }
                jsonArgs += "]";
                
                if (messageCallback) {
                    messageCallback(clientName, addr, jsonArgs);
                }

                if (loggingEnabled && logCallback) {
                    std::string logMsg = "[" + clientName + "] " + addr + " " + jsonArgs;
                    logCallback(logMsg);
                }

                // Internal Relay: If message is to /plugin/send, forward it out
                if (addr == "/plugin/send") {
                    // Expects: [address, format, arg1, arg2, ...]
                    // Note: This allows the browser to send OSC OUT via the Python relay
                    blog(LOG_INFO, "[OSC Server] Internal Relay triggered for %s", addr.c_str());
                    // Logic to forward will go here or be handled by the browser
                }

                // Add system log to confirm this plugin received it
                blog(LOG_INFO, "[OSC Server] Received: %s from %s", addr.c_str(), clientName.c_str());
            }
        }
    }
}

void OscManager::HttpListenerThread() {
    while (running) {
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientSocket = accept(httpSocket, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientSocket == INVALID_SOCKET) {
            if (running) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        char buffer[8192];
        int bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            std::string request(buffer);
            
            blog(LOG_DEBUG, "[OSC Server] HTTP Bridge received %d bytes", bytesRead);

            if (request.find("OPTIONS") != std::string::npos) {
                // Handle CORS preflight
                const char* corsResponse = "HTTP/1.1 204 No Content\r\n"
                                         "Access-Control-Allow-Origin: *\r\n"
                                         "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
                                         "Access-Control-Allow-Headers: Content-Type\r\n"
                                         "Connection: close\r\n\r\n";
                send(clientSocket, corsResponse, strlen(corsResponse), 0);
            }
            else if (request.find("POST /send") != std::string::npos) {
                blog(LOG_DEBUG, "[OSC Server] HTTP Bridge matched POST /send");
                size_t bodyPos = request.find("\r\n\r\n");
                if (bodyPos != std::string::npos) {
                    std::string body = request.substr(bodyPos + 4);
                    blog(LOG_DEBUG, "[OSC Server] HTTP Bridge body: %s", body.c_str());
                    
                    obs_data_t* data = obs_data_create_from_json(body.c_str());
                    if (data) {
                        const char* addr = obs_data_get_string(data, "address");
                        const char* fmt = obs_data_get_string(data, "format");
                        obs_data_array_t* args = obs_data_get_array(data, "args");
                        
                        if (addr && fmt) {
                            blog(LOG_INFO, "[OSC Server] HTTP Bridge forwarding: %s %s", addr, fmt);
                            SendOscRaw(addr, fmt, args);
                        } else {
                            blog(LOG_WARNING, "[OSC Server] HTTP Bridge missing addr or fmt in JSON");
                        }
                        
                        obs_data_array_release(args);
                        obs_data_release(data);
                    } else {
                        blog(LOG_ERROR, "[OSC Server] HTTP Bridge failed to parse JSON body");
                    }
                } else {
                    blog(LOG_WARNING, "[OSC Server] HTTP Bridge could not find end of headers");
                }
                
                const char* response = "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: text/plain\r\n"
                                     "Access-Control-Allow-Origin: *\r\n"
                                     "Content-Length: 2\r\n"
                                     "Connection: close\r\n\r\nok";
                send(clientSocket, response, strlen(response), 0);
            } else {
                blog(LOG_DEBUG, "[OSC Server] HTTP Bridge ignored request: %s", request.substr(0, 50).c_str());
                const char* response = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
                send(clientSocket, response, strlen(response), 0);
            }
        }
#if defined(_WIN32) || defined(_WIN64)
        closesocket(clientSocket);
#else
        close(clientSocket);
#endif
    }
}

void OscManager::SendOscMessage(const std::string& address, const char* format, ...) {
    va_list ap;
    char buffer[4096];
    
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (const auto& client : clients) {
        if (client.portOut <= 0) continue;

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) continue;

        struct sockaddr_in destAddr;
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(client.portOut);
        destAddr.sin_addr.s_addr = inet_addr(client.ip.c_str());

        va_start(ap, format);
        uint32_t len = tosc_vwriteMessage(buffer, sizeof(buffer), address.c_str(), format, ap);
        va_end(ap);
        
        sendto(sock, buffer, len, 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
#if defined(_WIN32) || defined(_WIN64)
        closesocket(sock);
#else
        close(sock);
#endif
    }
}

void OscManager::SendOscToClient(const std::string& clientName, const std::string& address, const char* format, ...) {
    va_list ap;
    char buffer[4096];
    
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (const auto& client : clients) {
        if (client.name != clientName || client.portOut <= 0) continue;

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) continue;

        struct sockaddr_in destAddr;
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(client.portOut);
        destAddr.sin_addr.s_addr = inet_addr(client.ip.c_str());

        va_start(ap, format);
        uint32_t len = tosc_vwriteMessage(buffer, sizeof(buffer), address.c_str(), format, ap);
        va_end(ap);
        
        sendto(sock, buffer, len, 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
#if defined(_WIN32) || defined(_WIN64)
        closesocket(sock);
#else
        close(sock);
#endif
        break;
    }
}

void OscManager::SendOscRaw(const std::string& address, const std::string& format, struct obs_data_array* args) {
    char buffer[4096];
    memset(buffer, 0, sizeof(buffer));
    
    uint32_t i = (uint32_t)address.length();
    if (i >= sizeof(buffer)) return;
    strcpy(buffer, address.c_str());
    i = (i + 4) & ~0x3;
    
    buffer[i++] = ',';
    int s_len = (int)format.length();
    if (i + s_len >= (int)sizeof(buffer)) return;
    strcpy(buffer + i, format.c_str());
    i = (i + 4 + s_len) & ~0x3;

    size_t count = obs_data_array_count(args);
    for (int j = 0; j < (int)format.length() && (size_t)j < count; ++j) {
        obs_data_t* item = obs_data_array_item(args, j);
        if (!item) continue;

        switch (format[j]) {
            case 'i': {
                if (i + 4 > sizeof(buffer)) break;
                int32_t val = (int32_t)obs_data_get_int(item, "value");
                *((uint32_t *)(buffer + i)) = (uint32_t)htonl(val);
                i += 4;
                break;
            }
            case 'f': {
                if (i + 4 > sizeof(buffer)) break;
                float val = (float)obs_data_get_double(item, "value");
                *((uint32_t *)(buffer + i)) = (uint32_t)htonl(*((uint32_t *)&val));
                i += 4;
                break;
            }
            case 'd': {
                if (i + 8 > sizeof(buffer)) break;
                double val = obs_data_get_double(item, "value");
                *((uint64_t *)(buffer + i)) = (uint64_t)htonll(*((uint64_t *)&val));
                i += 8;
                break;
            }
            case 's': {
                const char* s = obs_data_get_string(item, "value");
                int slen = (int)strlen(s);
                if (i + slen >= sizeof(buffer)) break;
                strcpy(buffer + i, s);
                i = (i + slen + 4) & ~0x3;
                break;
            }
        }
        obs_data_release(item);
    }

    // Send to all clients
    std::lock_guard<std::mutex> lock(clientsMutex);
    for (const auto& client : clients) {
        if (client.portOut <= 0) continue;

        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) continue;

        struct sockaddr_in destAddr;
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(client.portOut);
        destAddr.sin_addr.s_addr = inet_addr(client.ip.c_str());

        sendto(sock, buffer, i, 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
#if defined(_WIN32) || defined(_WIN64)
        closesocket(sock);
#else
        close(sock);
#endif
    }
}

void OscManager::LoadConfig() {
    char* configPath = obs_module_config_path("osc-server-settings.json");
    if (!configPath) return;

    obs_data_t* data = obs_data_create_from_json_file(configPath);
    bfree(configPath);

    if (data) {
        const char* ip = obs_data_get_string(data, "server_ip");
        int port = (int)obs_data_get_int(data, "server_port");
        int bPort = (int)obs_data_get_int(data, "bridge_port");
        const char* target = obs_data_get_string(data, "target_source");
        
        if (ip && *ip) serverIp = ip;
        if (port > 0) serverPort = port;
        if (bPort > 0) httpPort = bPort;
        if (target) targetSource = target;

        std::lock_guard<std::mutex> lock(clientsMutex);
        clients.clear();
        
        obs_data_array_t* clientsArray = obs_data_get_array(data, "clients");
        if (clientsArray) {
            size_t count = obs_data_array_count(clientsArray);
            for (size_t i = 0; i < count; i++) {
                obs_data_t* clientData = obs_data_array_item(clientsArray, i);
                OscClient client;
                client.name = obs_data_get_string(clientData, "name");
                client.ip = obs_data_get_string(clientData, "ip");
                client.portOut = (int)obs_data_get_int(clientData, "portOut");
                clients.push_back(client);
                obs_data_release(clientData);
            }
            obs_data_array_release(clientsArray);
        }
        obs_data_release(data);
    }
}

OscManager& GetOscManager() {
    static OscManager instance;
    return instance;
}
