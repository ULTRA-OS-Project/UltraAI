// UltraAITextLLM.h — chat / completion capability.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <memory>
#include <optional>

namespace UltraAI {

// ---------------------------------------------------------------- Messages

enum class Role { System, User, Assistant, Tool };

// One piece of multimodal content inside a message.
struct ContentPart {
    enum class Kind { Text, Media } kind = Kind::Text;
    std::string text;   // Kind::Text
    MediaBlob   media;  // Kind::Media (image, audio, document, ...)

    static ContentPart Text(std::string t) { ContentPart p; p.kind = Kind::Text; p.text = std::move(t); return p; }
    static ContentPart Media_(MediaBlob m) { ContentPart p; p.kind = Kind::Media; p.media = std::move(m); return p; }
};

// A tool call requested by the model.
struct ToolCall {
    std::string id;
    std::string name;
    std::string argumentsJson;
};

struct Message {
    Role                     role = Role::User;
    std::string              text;         // convenience for pure-text messages
    std::vector<ContentPart> parts;        // multimodal content (used if non-empty)
    std::vector<ToolCall>    toolCalls;    // Role::Assistant tool requests
    std::string              toolCallId;   // Role::Tool: which call this answers
};

// ---------------------------------------------------------------- Tools

struct ToolSpec {
    std::string name;
    std::string description;
    std::string parametersJsonSchema; // JSON Schema for the arguments object
};

// ---------------------------------------------------------------- Request / Response

struct ChatRequest {
    std::vector<Message>  messages;
    std::vector<ToolSpec> tools;
    std::string           responseJsonSchema; // non-empty -> structured output
    std::optional<double> temperature;
    std::optional<int>    maxTokens;
    OptionsMap            options;
};

enum class FinishReason { Stop, MaxTokens, ToolUse, ContentFilter, Unknown };

struct ChatResponse {
    std::string           text;
    std::vector<ToolCall> toolCalls;
    FinishReason          finishReason = FinishReason::Unknown;
    TokenUsage            usage;
    Error                 error;
};

// ---------------------------------------------------------------- Streaming

enum class StreamEventKind { TextDelta, ToolCallDelta, Usage, Done, Error };

struct StreamEvent {
    StreamEventKind kind = StreamEventKind::TextDelta;
    std::string     textDelta; // TextDelta
    ToolCall        toolCall;  // ToolCallDelta (may arrive incrementally)
    TokenUsage      usage;     // Usage / Done
    Error           error;     // Error
};

using StreamCallback = std::function<void(const StreamEvent&)>;

// ---------------------------------------------------------------- Interface

struct TextLLMConfig : ProviderConfig {};

class ITextLLM : public IProvider {
public:
    virtual ChatResponse Chat(const ChatRequest& request) = 0;

    // Streams deltas to `onEvent`; terminates with Done or Error.
    virtual Error ChatStream(const ChatRequest& request, StreamCallback onEvent) = 0;

    // Optional: provider-side token counting. Returns 0 if unsupported.
    virtual std::uint64_t CountTokens(const ChatRequest& /*request*/) { return 0; }
};

// ---------------------------------------------------------------- Factory

using TextLLMFactory = std::function<std::unique_ptr<ITextLLM>(const TextLLMConfig&)>;

std::unique_ptr<ITextLLM> CreateTextLLM(const TextLLMConfig& config, Error* error = nullptr);
std::vector<std::string>  ListTextLLMProviders();
bool RegisterTextLLMProvider(const std::string& name, TextLLMFactory factory,
                             bool makeDefault = false);

} // namespace UltraAI
