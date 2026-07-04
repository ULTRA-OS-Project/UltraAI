// FakeLocalServer.h — in-process HTTP server for local-adapter tests.
// Binds 127.0.0.1 on an ephemeral port and serves canned handlers, so the
// adapter test suite runs with no external model server (the record/replay
// principle from Docs/UltraNetIntegration.md).
// Part of ULTRA OS · MIT license · Cloverleaf UG
#pragma once

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  namespace testserver {
  using SocketT = SOCKET;
  const SocketT kBadSocket = INVALID_SOCKET;
  inline void CloseSocket(SocketT s) { closesocket(s); }
  inline bool SocketValid(SocketT s) { return s != INVALID_SOCKET; }
  inline void InitSockets() {
      static const bool once = [] {
          WSADATA d;
          WSAStartup(MAKEWORD(2, 2), &d);
          return true;
      }();
      (void)once;
  }
  } // namespace testserver
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  namespace testserver {
  using SocketT = int;
  const SocketT kBadSocket = -1;
  inline void CloseSocket(SocketT s) { ::close(s); }
  inline bool SocketValid(SocketT s) { return s >= 0; }
  inline void InitSockets() {}
  } // namespace testserver
#endif

namespace testserver {

struct Request {
    std::string method;
    std::string path;
    std::string headers; // raw header block
    std::string body;
};

struct Response {
    int         status = 200;
    std::string contentType = "application/json";
    std::string body;
};

class FakeLocalServer {
public:
    using Handler = std::function<Response(const Request&)>;

    FakeLocalServer() {
        InitSockets();
        listen_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = 0; // ephemeral
        bind(listen_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        listen(listen_, 8);
        socklen_t len = sizeof(addr);
        getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { Loop(); });
    }

    ~FakeLocalServer() {
        running_ = false;
#ifdef _WIN32
        CloseSocket(listen_);
#else
        shutdown(listen_, SHUT_RDWR);
        CloseSocket(listen_);
#endif
        if (thread_.joinable()) thread_.join();
    }

    int port() const { return port_; }
    std::string BaseUrl() const { return "http://127.0.0.1:" + std::to_string(port_); }

    void Handle(const std::string& path, Handler handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[path] = std::move(handler);
    }

    Request LastRequest() {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_;
    }

private:
    void Loop() {
        while (running_) {
            SocketT conn = accept(listen_, nullptr, nullptr);
            if (!SocketValid(conn)) break;
            Serve(conn);
            CloseSocket(conn);
        }
    }

    void Serve(SocketT conn) {
        std::string raw;
        char buf[4096];
        std::size_t headEnd = std::string::npos;
        std::size_t contentLength = 0;
        while (true) {
            if (headEnd == std::string::npos) {
                headEnd = raw.find("\r\n\r\n");
                if (headEnd != std::string::npos) {
                    std::string head = raw.substr(0, headEnd);
                    std::size_t cl = LowerCase(head).find("content-length:");
                    if (cl != std::string::npos)
                        contentLength = static_cast<std::size_t>(
                            std::strtoul(head.c_str() + cl + 15, nullptr, 10));
                }
            }
            if (headEnd != std::string::npos &&
                raw.size() >= headEnd + 4 + contentLength)
                break;
            auto n = recv(conn, buf, sizeof(buf), 0);
            if (n <= 0) return;
            raw.append(buf, static_cast<std::size_t>(n));
        }

        Request req;
        std::size_t sp1 = raw.find(' ');
        std::size_t sp2 = raw.find(' ', sp1 + 1);
        req.method  = raw.substr(0, sp1);
        req.path    = raw.substr(sp1 + 1, sp2 - sp1 - 1);
        req.headers = raw.substr(0, headEnd);
        req.body    = raw.substr(headEnd + 4, contentLength);

        Handler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            last_ = req;
            auto it = handlers_.find(req.path);
            if (it != handlers_.end()) handler = it->second;
        }
        Response resp;
        if (handler) {
            resp = handler(req);
        } else {
            resp.status = 404;
            resp.body   = "{\"error\":\"no handler for " + req.path + "\"}";
        }

        std::string out = "HTTP/1.1 " + std::to_string(resp.status) + " X\r\n";
        out += "Content-Type: " + resp.contentType + "\r\n";
        out += "Content-Length: " + std::to_string(resp.body.size()) + "\r\n";
        out += "Connection: close\r\n\r\n";
        out += resp.body;
        std::size_t sent = 0;
        while (sent < out.size()) {
            auto n = send(conn, out.data() + sent,
                          static_cast<int>(out.size() - sent), 0);
            if (n <= 0) return;
            sent += static_cast<std::size_t>(n);
        }
    }

    static std::string LowerCase(std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    SocketT                        listen_ = kBadSocket;
    int                            port_ = 0;
    std::atomic<bool>              running_{ true };
    std::thread                    thread_;
    std::mutex                     mutex_;
    std::map<std::string, Handler> handlers_;
    Request                        last_;
};

} // namespace testserver
