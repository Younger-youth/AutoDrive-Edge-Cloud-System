#ifndef PUBSUB_H
#define PUBSUB_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <map>
#include <functional>
#include <algorithm>
#include <memory>
#include "json.hpp"

// Winsock initialization helper
inline void init_winsock() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        WSADATA wsaData;
        int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (res != 0) {
            std::cerr << "[ERROR] WSAStartup failed: " << res << std::endl;
        }
    });
}

// Base64 helper methods
static const std::string b64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

inline std::string base64_encode(const unsigned char* bytes_to_encode, unsigned int in_len) {
    std::string ret;
    int i = 0, j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += b64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += b64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;
}

inline std::string base64_decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[b64_chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Length-prefixed framing send/receive
inline bool send_packet(SOCKET sock, const std::string& msg) {
    uint32_t len = htonl(static_cast<uint32_t>(msg.size()));
    int sent = send(sock, reinterpret_cast<const char*>(&len), 4, 0);
    if (sent <= 0) return false;
    
    size_t total_sent = 0;
    while (total_sent < msg.size()) {
        int s = send(sock, msg.data() + total_sent, static_cast<int>(msg.size() - total_sent), 0);
        if (s <= 0) return false;
        total_sent += s;
    }
    return true;
}

inline bool recv_all(SOCKET sock, char* buf, int bytes_to_read) {
    int total_read = 0;
    while (total_read < bytes_to_read) {
        int r = recv(sock, buf + total_read, bytes_to_read - total_read, 0);
        if (r <= 0) return false;
        total_read += r;
    }
    return true;
}

inline bool recv_packet(SOCKET sock, std::string& msg) {
    uint32_t len_n;
    if (!recv_all(sock, reinterpret_cast<char*>(&len_n), 4)) return false;
    uint32_t len = ntohl(len_n);
    if (len == 0) {
        msg.clear();
        return true;
    }
    msg.resize(len);
    if (!recv_all(sock, &msg[0], len)) return false;
    return true;
}

// Message Broker class
class PubSubBroker {
public:
    PubSubBroker(int port = 9000) : port_(port), listen_sock_(INVALID_SOCKET), running_(false) {}
    
    ~PubSubBroker() {
        stop();
    }

    void start() {
        init_winsock();
        listen_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock_ == INVALID_SOCKET) {
            std::cerr << "[Broker] Create socket failed" << std::endl;
            return;
        }

        char opt = 1;
        setsockopt(listen_sock_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port_);

        if (bind(listen_sock_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            std::cerr << "[Broker] Bind failed on port " << port_ << std::endl;
            closesocket(listen_sock_);
            return;
        }

        if (listen(listen_sock_, SOMAXCONN) == SOCKET_ERROR) {
            std::cerr << "[Broker] Listen failed" << std::endl;
            closesocket(listen_sock_);
            return;
        }

        running_ = true;
        std::cout << "[Broker] Message Hub running on port " << port_ << "..." << std::endl;

        accept_thread_ = std::thread(&PubSubBroker::accept_loop, this);
        accept_thread_.join();
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        closesocket(listen_sock_);

        std::lock_guard<std::mutex> lock(mutex_);
        for (SOCKET s : clients_) {
            closesocket(s);
        }
        clients_.clear();
        subscriptions_.clear();
        
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
    }

private:
    void accept_loop() {
        while (running_) {
            SOCKET client_sock = accept(listen_sock_, nullptr, nullptr);
            if (client_sock == INVALID_SOCKET) {
                break;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_.push_back(client_sock);
            }
            std::thread(&PubSubBroker::client_handler, this, client_sock).detach();
        }
    }

    void client_handler(SOCKET sock) {
        std::string msg;
        while (running_) {
            if (!recv_packet(sock, msg)) {
                break;
            }

            try {
                auto j = nlohmann::json::parse(msg);
                std::string type = j.value("type", "");
                std::string topic = j.value("topic", "");

                if (type == "subscribe") {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto& subs = subscriptions_[topic];
                    if (std::find(subs.begin(), subs.end(), sock) == subs.end()) {
                        subs.push_back(sock);
                    }
                } else if (type == "publish") {
                    nlohmann::json fwd;
                    fwd["type"] = "message";
                    fwd["topic"] = topic;
                    fwd["payload"] = j["payload"];
                    std::string fwd_str = fwd.dump();

                    std::vector<SOCKET> targets;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (subscriptions_.count(topic)) {
                            targets = subscriptions_[topic];
                        }
                    }

                    for (SOCKET t : targets) {
                        send_packet(t, fwd_str);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[Broker] Message processing error: " << e.what() << std::endl;
            }
        }

        closesocket(sock);
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), sock), clients_.end());
        for (auto& pair : subscriptions_) {
            auto& subs = pair.second;
            subs.erase(std::remove(subs.begin(), subs.end(), sock), subs.end());
        }
    }

    int port_;
    SOCKET listen_sock_;
    bool running_;
    std::thread accept_thread_;
    std::mutex mutex_;
    std::vector<SOCKET> clients_;
    std::map<std::string, std::vector<SOCKET>> subscriptions_;
};

// Client Node library
class PubSubClient {
public:
    PubSubClient(const std::string& host = "127.0.0.1", int port = 9000) 
        : host_(host), port_(port), sock_(INVALID_SOCKET), running_(false) {}

    ~PubSubClient() {
        disconnect();
    }

    bool connect_to_broker() {
        init_winsock();
        sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ == INVALID_SOCKET) {
            std::cerr << "[Client] Socket creation failed" << std::endl;
            return false;
        }

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

        int retries = 10;
        while (retries--) {
            if (connect(sock_, (sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR) {
                break;
            }
            if (retries == 0) {
                std::cerr << "[Client] Connection to broker failed!" << std::endl;
                closesocket(sock_);
                sock_ = INVALID_SOCKET;
                return false;
            }
            std::cout << "[Client] Broker not active, retrying in 1s..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        running_ = true;
        recv_thread_ = std::thread(&PubSubClient::receive_loop, this);
        return true;
    }

    void disconnect() {
        if (!running_) return;
        running_ = false;
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        if (recv_thread_.joinable()) {
            recv_thread_.join();
        }
    }

    void subscribe(const std::string& topic, std::function<void(const nlohmann::json&)> callback) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callbacks_[topic].push_back(callback);
        }

        nlohmann::json sub;
        sub["type"] = "subscribe";
        sub["topic"] = topic;
        send_packet(sock_, sub.dump());
    }

    void publish(const std::string& topic, const nlohmann::json& payload) {
        nlohmann::json pub;
        pub["type"] = "publish";
        pub["topic"] = topic;
        pub["payload"] = payload;
        send_packet(sock_, pub.dump());
    }

private:
    void receive_loop() {
        std::string msg;
        while (running_) {
            if (!recv_packet(sock_, msg)) {
                break;
            }

            try {
                auto j = nlohmann::json::parse(msg);
                std::string type = j.value("type", "");
                if (type == "message") {
                    std::string topic = j.value("topic", "");
                    nlohmann::json payload = j["payload"];

                    std::vector<std::function<void(const nlohmann::json&)>> targets;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (callbacks_.count(topic)) {
                            targets = callbacks_[topic];
                        }
                    }

                    for (auto& cb : targets) {
                        cb(payload);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[Client] Message dispatch error: " << e.what() << std::endl;
            }
        }
        running_ = false;
    }

    std::string host_;
    int port_;
    SOCKET sock_;
    bool running_;
    std::thread recv_thread_;
    std::mutex mutex_;
    std::map<std::string, std::vector<std::function<void(const nlohmann::json&)>>> callbacks_;
};

#endif // PUBSUB_H
