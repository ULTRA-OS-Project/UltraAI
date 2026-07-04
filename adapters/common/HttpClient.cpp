// HttpClient.cpp — loopback-only HTTP/1.1 over BSD sockets / Winsock.
// Part of ULTRA OS · MIT license · Cloverleaf UG
#include "HttpClient.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  namespace {
  using SocketT = SOCKET;
  const SocketT kBadSocket = INVALID_SOCKET;
  void CloseSocket(SocketT s) { closesocket(s); }
  bool SocketValid(SocketT s) { return s != INVALID_SOCKET; }
  void EnsureSocketsInitialized() {
      static const bool once = [] {
          WSADATA data;
          WSAStartup(MAKEWORD(2, 2), &data);
          return true;
      }();
      (void)once;
  }
  void SetRecvTimeout(SocketT s, int seconds) {
      DWORD ms = static_cast<DWORD>(seconds) * 1000;
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&ms), sizeof(ms));
  }
  } // namespace
#else
  #include <netdb.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  namespace {
  using SocketT = int;
  const SocketT kBadSocket = -1;
  void CloseSocket(SocketT s) { ::close(s); }
  bool SocketValid(SocketT s) { return s >= 0; }
  void EnsureSocketsInitialized() {}
  void SetRecvTimeout(SocketT s, int seconds) {
      struct timeval tv;
      tv.tv_sec  = seconds;
      tv.tv_usec = 0;
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  }
  } // namespace
#endif

namespace UltraAI {
namespace localhttp {

namespace {

// ---------------------------------------------------------------- URL

struct ParsedUrl {
    std::string host;
    std::string port = "80";
    std::string path = "/";
};

bool ParseUrl(const std::string& url, ParsedUrl* out, std::string* err) {
    const std::string scheme = "http://";
    if (url.compare(0, scheme.size(), scheme) != 0) {
        *err = "only plain http:// URLs are supported by the local transport";
        return false;
    }
    std::string rest = url.substr(scheme.size());
    std::size_t slash = rest.find('/');
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    out->path = slash == std::string::npos ? "/" : rest.substr(slash);
    if (hostport.empty()) { *err = "missing host"; return false; }

    if (hostport[0] == '[') { // [::1]:port
        std::size_t close = hostport.find(']');
        if (close == std::string::npos) { *err = "malformed IPv6 host"; return false; }
        out->host = hostport.substr(1, close - 1);
        if (close + 1 < hostport.size() && hostport[close + 1] == ':')
            out->port = hostport.substr(close + 2);
    } else {
        std::size_t colon = hostport.find(':');
        out->host = colon == std::string::npos ? hostport : hostport.substr(0, colon);
        if (colon != std::string::npos) out->port = hostport.substr(colon + 1);
    }
    return true;
}

bool IsLoopbackHost(const std::string& host) {
    if (host == "localhost" || host == "::1") return true;
    return host.compare(0, 4, "127.") == 0;
}

// ---------------------------------------------------------------- response parsing

std::string ToLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct ResponseHead {
    int                                status = 0;
    std::map<std::string, std::string> headers;
};

bool ParseHead(const std::string& raw, ResponseHead* out) {
    std::size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos) return false;
    // "HTTP/1.1 200 OK"
    std::size_t sp = raw.find(' ');
    if (sp == std::string::npos || sp + 4 > lineEnd) return false;
    out->status = std::atoi(raw.c_str() + sp + 1);
    std::size_t pos = lineEnd + 2;
    while (pos < raw.size()) {
        std::size_t end = raw.find("\r\n", pos);
        if (end == std::string::npos || end == pos) break;
        std::string line = raw.substr(pos, end - pos);
        std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = ToLower(line.substr(0, colon));
            std::size_t vstart = colon + 1;
            while (vstart < line.size() && line[vstart] == ' ') ++vstart;
            out->headers[key] = line.substr(vstart);
        }
        pos = end + 2;
    }
    return out->status > 0;
}

// Incremental "Transfer-Encoding: chunked" decoder.
class ChunkDecoder {
public:
    // Feeds raw bytes; forwards decoded payload bytes to sink.
    // Returns false on malformed framing.
    bool Feed(const char* data, std::size_t size,
              const std::function<void(const char*, std::size_t)>& sink) {
        buf_.append(data, size);
        while (true) {
            if (done_) return true;
            if (remaining_ == 0) {
                std::size_t lineEnd = buf_.find("\r\n");
                if (lineEnd == std::string::npos) return true; // need more
                std::size_t parsed = 0;
                long size_ = std::strtol(buf_.c_str(), nullptr, 16);
                parsed = lineEnd + 2;
                if (size_ < 0) return false;
                buf_.erase(0, parsed);
                if (size_ == 0) { done_ = true; return true; }
                remaining_ = static_cast<std::size_t>(size_);
            }
            std::size_t take = remaining_ < buf_.size() ? remaining_ : buf_.size();
            if (take == 0) return true; // need more data
            sink(buf_.data(), take);
            buf_.erase(0, take);
            remaining_ -= take;
            if (remaining_ == 0) {
                if (buf_.size() < 2) return true; // wait for trailing CRLF
                if (buf_[0] != '\r' || buf_[1] != '\n') return false;
                buf_.erase(0, 2);
            }
        }
    }
    bool done() const { return done_; }

private:
    std::string buf_;
    std::size_t remaining_ = 0;
    bool        done_ = false;
};

// ---------------------------------------------------------------- request

Error Perform(const HttpRequest& request, ResponseHead* head,
              const std::function<void(const char*, std::size_t)>& bodySink) {
    EnsureSocketsInitialized();

    ParsedUrl url;
    std::string err;
    if (!ParseUrl(request.url, &url, &err))
        return Error::Make(ErrorCode::InvalidArgument, err + ": " + request.url);
    if (!IsLoopbackHost(url.host))
        return Error::Make(ErrorCode::Network,
                           "local transport only reaches loopback hosts, refusing: " + url.host);

    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &res) != 0 || !res)
        return Error::Make(ErrorCode::Network, "cannot resolve " + url.host);

    SocketT sock = kBadSocket;
    for (struct addrinfo* ai = res; ai; ai = ai->ai_next) {
        sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (!SocketValid(sock)) continue;
        if (connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) break;
        CloseSocket(sock);
        sock = kBadSocket;
    }
    freeaddrinfo(res);
    if (!SocketValid(sock))
        return Error::Make(ErrorCode::Network,
                           "cannot connect to " + url.host + ":" + url.port);
    SetRecvTimeout(sock, request.timeoutSeconds);

    // Build and send the request.
    std::string msg = request.method + " " + url.path + " HTTP/1.1\r\n";
    msg += "Host: " + url.host + ":" + url.port + "\r\n";
    msg += "Connection: close\r\n";
    msg += "Accept: */*\r\n";
    if (!request.body.empty() || request.method == "POST" || request.method == "PUT") {
        msg += "Content-Type: " + request.contentType + "\r\n";
        msg += "Content-Length: " + std::to_string(request.body.size()) + "\r\n";
    }
    for (const auto& h : request.headers) msg += h.first + ": " + h.second + "\r\n";
    msg += "\r\n";
    msg += request.body;

    std::size_t sent = 0;
    while (sent < msg.size()) {
        auto n = send(sock, msg.data() + sent, static_cast<int>(msg.size() - sent), 0);
        if (n <= 0) {
            CloseSocket(sock);
            return Error::Make(ErrorCode::Network, "send failed to " + url.host);
        }
        sent += static_cast<std::size_t>(n);
    }

    // Read the response.
    std::string raw;
    bool headParsed = false;
    bool chunked = false;
    std::size_t contentLength = 0;
    bool haveContentLength = false;
    std::size_t bodyDelivered = 0;
    ChunkDecoder decoder;
    char buf[8192];

    while (true) {
        auto n = recv(sock, buf, sizeof(buf), 0);
        if (n < 0) {
            CloseSocket(sock);
            return Error::Make(ErrorCode::Timeout,
                               "recv failed or timed out reading from " + url.host);
        }
        if (n == 0) break; // connection closed
        if (!headParsed) {
            raw.append(buf, static_cast<std::size_t>(n));
            std::size_t headEnd = raw.find("\r\n\r\n");
            if (headEnd == std::string::npos) continue;
            if (!ParseHead(raw.substr(0, headEnd + 2), head)) {
                CloseSocket(sock);
                return Error::Make(ErrorCode::Network, "malformed HTTP response");
            }
            headParsed = true;
            auto te = head->headers.find("transfer-encoding");
            chunked = te != head->headers.end() &&
                      ToLower(te->second).find("chunked") != std::string::npos;
            auto cl = head->headers.find("content-length");
            if (cl != head->headers.end()) {
                haveContentLength = true;
                contentLength = static_cast<std::size_t>(std::strtoul(cl->second.c_str(), nullptr, 10));
            }
            std::string firstBody = raw.substr(headEnd + 4);
            raw.clear();
            if (!firstBody.empty()) {
                if (chunked) {
                    if (!decoder.Feed(firstBody.data(), firstBody.size(), bodySink)) {
                        CloseSocket(sock);
                        return Error::Make(ErrorCode::Network, "malformed chunked body");
                    }
                } else {
                    bodySink(firstBody.data(), firstBody.size());
                    bodyDelivered += firstBody.size();
                }
            }
        } else {
            if (chunked) {
                if (!decoder.Feed(buf, static_cast<std::size_t>(n), bodySink)) {
                    CloseSocket(sock);
                    return Error::Make(ErrorCode::Network, "malformed chunked body");
                }
            } else {
                bodySink(buf, static_cast<std::size_t>(n));
                bodyDelivered += static_cast<std::size_t>(n);
            }
        }
        if (headParsed) {
            if (chunked && decoder.done()) break;
            if (!chunked && haveContentLength && bodyDelivered >= contentLength) break;
        }
    }
    CloseSocket(sock);
    if (!headParsed)
        return Error::Make(ErrorCode::Network, "connection closed before response headers");
    return {};
}

} // namespace

// ---------------------------------------------------------------- public API

Error MapHttpStatus(int status, const std::string& body) {
    if (status >= 200 && status < 300) return {};
    std::string snippet = body.substr(0, 200);
    std::string msg = "HTTP " + std::to_string(status) +
                      (snippet.empty() ? "" : ": " + snippet);
    if (status == 401 || status == 403) return Error::Make(ErrorCode::AuthFailure, msg);
    if (status == 429)                  return Error::Make(ErrorCode::RateLimited, msg);
    if (status >= 400 && status < 500)  return Error::Make(ErrorCode::InvalidArgument, msg);
    if (status >= 500)                  return Error::Make(ErrorCode::Internal, msg);
    return Error::Make(ErrorCode::Network, msg);
}

Error Fetch(const HttpRequest& request, HttpResponse* response) {
    ResponseHead head;
    std::string body;
    Error err = Perform(request, &head,
                        [&body](const char* d, std::size_t n) { body.append(d, n); });
    if (err) return err;
    response->status  = head.status;
    response->headers = std::move(head.headers);
    response->body    = std::move(body);
    return {};
}

Error FetchStream(const HttpRequest& request, int* statusOut,
                  const std::function<void(const char* data, std::size_t size)>& onBody) {
    ResponseHead head;
    std::string errorBody;
    bool streaming = false; // becomes true once we know status is 2xx
    Error err = Perform(request, &head, [&](const char* d, std::size_t n) {
        // head.status is filled before the first body byte arrives.
        if (!streaming) {
            if (head.status >= 200 && head.status < 300) streaming = true;
            else { errorBody.append(d, n); return; }
        }
        onBody(d, n);
    });
    if (statusOut) *statusOut = head.status;
    if (err) return err;
    if (head.status < 200 || head.status >= 300)
        return MapHttpStatus(head.status, errorBody);
    return {};
}

} // namespace localhttp
} // namespace UltraAI
