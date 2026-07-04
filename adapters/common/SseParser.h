// SseParser.h — incremental Server-Sent-Events parser (Docs/UltraNetIntegration.md).
// Event framing is an AI-provider concern: this consumes raw byte callbacks
// from the transport and emits the `data:` payload of each complete event.
// Part of ULTRA OS · MIT license · Cloverleaf UG
#pragma once

#include <functional>
#include <string>

namespace UltraAI {
namespace sse {

class SseParser {
public:
    // onData receives the joined data payload of one complete event
    // (multiple `data:` lines are joined with '\n', per the SSE spec).
    void Feed(const char* data, std::size_t size,
              const std::function<void(const std::string& payload)>& onData);

private:
    void HandleLine(const std::string& line,
                    const std::function<void(const std::string&)>& onData);

    std::string buffer_;
    std::string dataAccum_;
    bool        haveData_ = false;
};

} // namespace sse
} // namespace UltraAI
