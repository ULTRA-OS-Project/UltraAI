// SseParser.cpp
// Part of ULTRA OS · MIT license · Cloverleaf UG
#include "SseParser.h"

namespace UltraAI {
namespace sse {

void SseParser::Feed(const char* data, std::size_t size,
                     const std::function<void(const std::string&)>& onData) {
    buffer_.append(data, size);
    std::size_t start = 0;
    while (true) {
        std::size_t nl = buffer_.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = buffer_.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        HandleLine(line, onData);
        start = nl + 1;
    }
    buffer_.erase(0, start);
}

void SseParser::HandleLine(const std::string& line,
                           const std::function<void(const std::string&)>& onData) {
    if (line.empty()) { // blank line terminates the event
        if (haveData_) onData(dataAccum_);
        dataAccum_.clear();
        haveData_ = false;
        return;
    }
    if (line[0] == ':') return; // comment
    if (line.compare(0, 5, "data:") == 0) {
        std::size_t start = 5;
        if (start < line.size() && line[start] == ' ') ++start;
        if (haveData_) dataAccum_ += '\n';
        dataAccum_ += line.substr(start);
        haveData_ = true;
    }
    // event:/id:/retry: fields are irrelevant to the provider protocols we speak.
}

} // namespace sse
} // namespace UltraAI
