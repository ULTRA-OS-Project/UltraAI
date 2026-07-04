// HttpClient.h — minimal blocking HTTP/1.1 client for LOCAL adapters.
//
// Transport seam: ULTRA OS policy is that UltraAI never opens sockets itself —
// all transport belongs to UltraNet. UltraNet does not exist yet, so this
// client fills the gap for local-model servers ONLY: it speaks plain http and
// refuses any host that is not loopback (localhost / 127.x / ::1). When
// UltraNet lands, this implementation is replaced behind the same functions
// (guarded by ULTRAAI_HAS_ULTRANET) without touching the adapters.
//
// Part of ULTRA OS · MIT license · Cloverleaf UG
#pragma once

#include "../../include/UltraAICommon.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace UltraAI {
namespace localhttp {

struct HttpRequest {
    std::string method = "POST";
    std::string url;                        // e.g. "http://127.0.0.1:8080/v1/chat/completions"
    std::string contentType = "application/json";
    std::vector<std::pair<std::string, std::string>> headers; // extra headers
    std::string body;
    int         timeoutSeconds = 300;       // generation can be slow on CPU
};

struct HttpResponse {
    int                                status = 0;
    std::map<std::string, std::string> headers; // keys lower-cased
    std::string                        body;
};

// Full round trip. Returns Error only for transport-level failures;
// HTTP status (including 4xx/5xx) is reported via response->status.
Error Fetch(const HttpRequest& request, HttpResponse* response);

// Streaming round trip for SSE and chunked bodies. Body bytes are delivered
// to onBody as they arrive (already de-chunked). If the server answers with
// a non-2xx status the body is collected internally instead and the mapped
// error is returned; onBody is only ever called for 2xx responses.
Error FetchStream(const HttpRequest& request, int* statusOut,
                  const std::function<void(const char* data, std::size_t size)>& onBody);

// Maps an HTTP status to the UltraAI error taxonomy
// (per Docs/UltraNetIntegration.md): 401/403 -> AuthFailure,
// 429 -> RateLimited, other 4xx -> InvalidArgument, 5xx -> Internal.
Error MapHttpStatus(int status, const std::string& body);

} // namespace localhttp
} // namespace UltraAI
