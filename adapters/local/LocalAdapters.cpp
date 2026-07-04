// LocalAdapters.cpp — provider "local": adapters for locally running model
// servers that expose the de-facto standard OpenAI-compatible HTTP API.
//
// One adapter family covers every server that speaks this protocol:
//   llama.cpp server / Ollama / vLLM ........ chat, embeddings, vision, FIM
//   whisper.cpp server / LocalAI ............ audio transcription
//   LocalAI / openedai-speech ............... speech synthesis, voices
//   LocalAI / sd-webui (OpenAI images API) ... image generation
//
// Endpoints used (relative to ProviderConfig::endpoint,
// default http://127.0.0.1:8080):
//   POST /v1/chat/completions      TextLLM, VisionAnalyzer, Translator, CodeAssist
//   POST /v1/embeddings            Embeddings
//   POST /v1/audio/transcriptions  SpeechToText (multipart)
//   POST /v1/audio/speech          TextToSpeech (binary response)
//   GET  /v1/audio/voices          TextToSpeech voice listing (extension; optional)
//   POST /v1/images/generations    ImageGen (b64_json response)
//
// VideoGen and MusicGen are NOT registered: no local server ecosystem has a
// settled API for them yet.
//
// Config:
//   endpoint   base URL, loopback only (the local transport refuses others)
//   model      forwarded verbatim ("" -> server default)
//   options["local.api_key"]  optional bearer token some local servers expect;
//                             for cloud adapters credentials will instead come
//                             from UltraVault via apiKeyRef.
//
// Live/streaming caveats: local servers do not do live microphone
// transcription, so ISttSession buffers pushed audio and transcribes on
// Finish(); ITextToSpeech::SpeakStream synthesizes fully, then chunks.
//
// Part of ULTRA OS · MIT license · Cloverleaf UG

#include "../../include/UltraAI.h"
#include "../common/Base64.h"
#include "../common/HttpClient.h"
#include "../common/MiniJson.h"
#include "../common/SseParser.h"

#include <algorithm>
#include <sstream>

namespace UltraAI {
namespace local {

using minijson::Value;

// ---------------------------------------------------------------- helpers

static std::string OptOf(const OptionsMap& o, const std::string& key,
                         const std::string& fallback = "") {
    auto it = o.find(key);
    return it == o.end() ? fallback : it->second;
}

static std::string BaseUrl(const ProviderConfig& cfg) {
    std::string url = cfg.endpoint.empty() ? "http://127.0.0.1:8080" : cfg.endpoint;
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

static localhttp::HttpRequest MakeRequest(const ProviderConfig& cfg,
                                          const std::string& path,
                                          std::string body,
                                          const std::string& method = "POST") {
    localhttp::HttpRequest req;
    req.method = method;
    req.url    = BaseUrl(cfg) + path;
    req.body   = std::move(body);
    const std::string key = OptOf(cfg.options, "local.api_key");
    if (!key.empty()) req.headers.push_back({ "Authorization", "Bearer " + key });
    return req;
}

// POST JSON, expect JSON back. Fills *out on success.
static Error PostJson(const ProviderConfig& cfg, const std::string& path,
                      const Value& payload, Value* out) {
    localhttp::HttpRequest req = MakeRequest(cfg, path, payload.Dump());
    localhttp::HttpResponse resp;
    Error err = localhttp::Fetch(req, &resp);
    if (err) return err;
    err = localhttp::MapHttpStatus(resp.status, resp.body);
    if (err) return err;
    std::string parseErr;
    if (!Value::Parse(resp.body, out, &parseErr))
        return Error::Make(ErrorCode::Internal, "malformed JSON from server: " + parseErr);
    return {};
}

static std::string DataUri(const MediaBlob& blob) {
    const std::string mime = blob.mimeType.empty() ? "image/png" : blob.mimeType;
    return "data:" + mime + ";base64," + base64::Encode(blob.data);
}

// Strips a single ```lang ... ``` fence if the whole reply is fenced.
static std::string StripCodeFence(const std::string& text) {
    std::size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos || text.compare(begin, 3, "```") != 0) return text;
    std::size_t firstNl = text.find('\n', begin);
    if (firstNl == std::string::npos) return text;
    std::size_t closing = text.rfind("```");
    if (closing <= firstNl) return text;
    std::string inner = text.substr(firstNl + 1, closing - firstNl - 1);
    while (!inner.empty() && (inner.back() == '\n' || inner.back() == '\r')) inner.pop_back();
    return inner;
}

static std::string Trimmed(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Shared IProvider boilerplate, mirroring the mock adapters.
template <typename ConfigT, typename InterfaceT>
class LocalBase : public InterfaceT {
public:
    explicit LocalBase(ConfigT cfg) : cfg_(std::move(cfg)) {}
    std::string ProviderId() const override { return "local"; }
    void* RawProvider() override { return this; }
protected:
    ConfigT cfg_;
};

// ---------------------------------------------------------------- chat protocol

static const char* RoleName(Role role) {
    switch (role) {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "tool";
    }
    return "user";
}

static Value MessageToJson(const Message& m) {
    Value msg = Value::MakeObject();
    msg.Set("role", RoleName(m.role));
    if (!m.parts.empty()) {
        Value content = Value::MakeArray();
        for (const auto& part : m.parts) {
            Value p = Value::MakeObject();
            if (part.kind == ContentPart::Kind::Text) {
                p.Set("type", "text").Set("text", part.text);
            } else {
                Value imageUrl = Value::MakeObject();
                imageUrl.Set("url", DataUri(part.media));
                p.Set("type", "image_url").Set("image_url", std::move(imageUrl));
            }
            content.Push(std::move(p));
        }
        msg.Set("content", std::move(content));
    } else {
        msg.Set("content", m.text);
    }
    if (!m.toolCalls.empty()) {
        Value calls = Value::MakeArray();
        for (const auto& call : m.toolCalls) {
            Value fn = Value::MakeObject();
            fn.Set("name", call.name).Set("arguments", call.argumentsJson);
            Value c = Value::MakeObject();
            c.Set("id", call.id).Set("type", "function").Set("function", std::move(fn));
            calls.Push(std::move(c));
        }
        msg.Set("tool_calls", std::move(calls));
    }
    if (m.role == Role::Tool && !m.toolCallId.empty())
        msg.Set("tool_call_id", m.toolCallId);
    return msg;
}

static Value ChatRequestToJson(const ProviderConfig& cfg, const ChatRequest& req,
                               bool stream) {
    Value body = Value::MakeObject();
    if (!cfg.model.empty()) body.Set("model", cfg.model);
    Value messages = Value::MakeArray();
    for (const auto& m : req.messages) messages.Push(MessageToJson(m));
    body.Set("messages", std::move(messages));

    if (!req.tools.empty()) {
        Value tools = Value::MakeArray();
        for (const auto& t : req.tools) {
            Value params;
            if (!Value::Parse(t.parametersJsonSchema, &params))
                params = Value::MakeObject().Set("type", "object");
            Value fn = Value::MakeObject();
            fn.Set("name", t.name).Set("description", t.description)
              .Set("parameters", std::move(params));
            Value tool = Value::MakeObject();
            tool.Set("type", "function").Set("function", std::move(fn));
            tools.Push(std::move(tool));
        }
        body.Set("tools", std::move(tools));
    }
    if (!req.responseJsonSchema.empty()) {
        Value schema;
        if (!Value::Parse(req.responseJsonSchema, &schema))
            schema = Value::MakeObject().Set("type", "object");
        Value jsonSchema = Value::MakeObject();
        jsonSchema.Set("name", "response").Set("schema", std::move(schema));
        Value fmt = Value::MakeObject();
        fmt.Set("type", "json_schema").Set("json_schema", std::move(jsonSchema));
        body.Set("response_format", std::move(fmt));
    }
    if (req.temperature) body.Set("temperature", *req.temperature);
    if (req.maxTokens)   body.Set("max_tokens", *req.maxTokens);
    if (stream) {
        body.Set("stream", true);
        Value opts = Value::MakeObject();
        opts.Set("include_usage", true);
        body.Set("stream_options", std::move(opts));
    }
    return body;
}

static FinishReason MapFinishReason(const std::string& reason) {
    if (reason == "stop")           return FinishReason::Stop;
    if (reason == "length")         return FinishReason::MaxTokens;
    if (reason == "tool_calls")     return FinishReason::ToolUse;
    if (reason == "content_filter") return FinishReason::ContentFilter;
    return FinishReason::Unknown;
}

static TokenUsage MapUsage(const Value& usage) {
    TokenUsage u;
    u.inputTokens  = usage.Get("prompt_tokens").AsUInt64();
    u.outputTokens = usage.Get("completion_tokens").AsUInt64();
    u.totalTokens  = usage.Get("total_tokens").AsUInt64(u.inputTokens + u.outputTokens);
    return u;
}

static std::vector<ToolCall> MapToolCalls(const Value& calls) {
    std::vector<ToolCall> out;
    for (const auto& c : calls.AsArray()) {
        ToolCall call;
        call.id            = c.Get("id").AsString();
        call.name          = c.Get("function").Get("name").AsString();
        call.argumentsJson = c.Get("function").Get("arguments").AsString();
        out.push_back(std::move(call));
    }
    return out;
}

// One non-streaming chat round trip; shared by the LLM-backed capabilities.
static Error ChatOnce(const ProviderConfig& cfg, const ChatRequest& req,
                      ChatResponse* out) {
    Value json;
    Error err = PostJson(cfg, "/v1/chat/completions",
                         ChatRequestToJson(cfg, req, /*stream=*/false), &json);
    if (err) return err;
    const auto& choices = json.Get("choices").AsArray();
    if (choices.empty())
        return Error::Make(ErrorCode::Internal, "server returned no choices");
    const Value& message = choices.front().Get("message");
    out->text         = message.Get("content").AsString();
    out->toolCalls    = MapToolCalls(message.Get("tool_calls"));
    out->finishReason = MapFinishReason(choices.front().Get("finish_reason").AsString());
    out->usage        = MapUsage(json.Get("usage"));
    return {};
}

// Convenience: system + user text (optionally with an image), reply text out.
static Error SimpleChat(const ProviderConfig& cfg, const std::string& system,
                        const std::string& userText, const MediaBlob* image,
                        std::string* replyOut) {
    ChatRequest req;
    if (!system.empty()) {
        Message sys; sys.role = Role::System; sys.text = system;
        req.messages.push_back(std::move(sys));
    }
    Message user; user.role = Role::User;
    if (image) {
        user.parts.push_back(ContentPart::Text(userText));
        user.parts.push_back(ContentPart::Media_(*image));
    } else {
        user.text = userText;
    }
    req.messages.push_back(std::move(user));
    ChatResponse resp;
    Error err = ChatOnce(cfg, req, &resp);
    if (err) return err;
    *replyOut = resp.text;
    return {};
}

// ---------------------------------------------------------------- TextLLM

class LocalTextLLM final : public LocalBase<TextLLMConfig, ITextLLM> {
public:
    using LocalBase::LocalBase;

    ChatResponse Chat(const ChatRequest& request) override {
        ChatResponse resp;
        if (request.messages.empty()) {
            resp.error = Error::Make(ErrorCode::InvalidArgument, "messages is empty");
            return resp;
        }
        resp.error = ChatOnce(cfg_, request, &resp);
        return resp;
    }

    Error ChatStream(const ChatRequest& request, StreamCallback onEvent) override {
        if (!onEvent) return Error::Make(ErrorCode::InvalidArgument, "null callback");
        if (request.messages.empty())
            return Error::Make(ErrorCode::InvalidArgument, "messages is empty");

        localhttp::HttpRequest http = MakeRequest(
            cfg_, "/v1/chat/completions",
            ChatRequestToJson(cfg_, request, /*stream=*/true).Dump());
        http.headers.push_back({ "Accept", "text/event-stream" });

        sse::SseParser parser;
        TokenUsage usage;
        bool sawDone = false;
        int status = 0;
        Error err = localhttp::FetchStream(http, &status,
            [&](const char* data, std::size_t size) {
                parser.Feed(data, size, [&](const std::string& payload) {
                    if (payload == "[DONE]") { sawDone = true; return; }
                    Value chunk;
                    if (!Value::Parse(payload, &chunk)) return;
                    if (chunk.Has("usage")) usage = MapUsage(chunk.Get("usage"));
                    const auto& choices = chunk.Get("choices").AsArray();
                    if (choices.empty()) return;
                    const Value& delta = choices.front().Get("delta");
                    const std::string content = delta.Get("content").AsString();
                    if (!content.empty()) {
                        StreamEvent ev;
                        ev.kind      = StreamEventKind::TextDelta;
                        ev.textDelta = content;
                        onEvent(ev);
                    }
                    for (const auto& call : delta.Get("tool_calls").AsArray()) {
                        StreamEvent ev;
                        ev.kind               = StreamEventKind::ToolCallDelta;
                        ev.toolCall.id            = call.Get("id").AsString();
                        ev.toolCall.name          = call.Get("function").Get("name").AsString();
                        ev.toolCall.argumentsJson = call.Get("function").Get("arguments").AsString();
                        onEvent(ev);
                    }
                });
            });
        if (err) {
            StreamEvent ev; ev.kind = StreamEventKind::Error; ev.error = err;
            onEvent(ev);
            return err;
        }
        (void)sawDone; // some servers close the stream without [DONE]; treat EOF as done
        StreamEvent done;
        done.kind  = StreamEventKind::Done;
        done.usage = usage;
        onEvent(done);
        return {};
    }
};

// ---------------------------------------------------------------- Embeddings

class LocalEmbeddings final : public LocalBase<EmbeddingsConfig, IEmbeddings> {
public:
    using LocalBase::LocalBase;

    EmbeddingsResponse Embed(const EmbeddingsRequest& request) override {
        EmbeddingsResponse resp;
        if (request.texts.empty()) {
            resp.error = Error::Make(ErrorCode::InvalidArgument, "texts is empty");
            return resp;
        }
        Value body = Value::MakeObject();
        if (!cfg_.model.empty()) body.Set("model", cfg_.model);
        Value input = Value::MakeArray();
        for (const auto& t : request.texts) input.Push(t);
        body.Set("input", std::move(input));
        if (request.dimensions > 0) body.Set("dimensions", request.dimensions);

        Value json;
        resp.error = PostJson(cfg_, "/v1/embeddings", body, &json);
        if (resp.error) return resp;

        for (const auto& item : json.Get("data").AsArray()) {
            std::vector<float> v;
            for (const auto& f : item.Get("embedding").AsArray())
                v.push_back(static_cast<float>(f.AsDouble()));
            resp.vectors.push_back(std::move(v));
        }
        if (resp.vectors.size() != request.texts.size()) {
            resp.error = Error::Make(ErrorCode::Internal,
                                     "server returned wrong number of embeddings");
            return resp;
        }
        resp.usage = MapUsage(json.Get("usage"));
        return resp;
    }
};

// ---------------------------------------------------------------- SpeechToText

static std::string AudioFileName(const std::string& mime) {
    if (mime == "audio/wav" || mime == "audio/x-wav") return "audio.wav";
    if (mime == "audio/mpeg" || mime == "audio/mp3")  return "audio.mp3";
    if (mime == "audio/ogg")  return "audio.ogg";
    if (mime == "audio/flac") return "audio.flac";
    return "audio.bin";
}

static void MultipartField(std::string& body, const std::string& boundary,
                           const std::string& name, const std::string& value) {
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n";
    body += value + "\r\n";
}

static TranscriptionResult ParseTranscription(const Value& json) {
    TranscriptionResult res;
    res.text             = json.Get("text").AsString();
    res.detectedLanguage = json.Get("language").AsString();
    for (const auto& s : json.Get("segments").AsArray()) {
        TranscriptSegment seg;
        seg.text     = Trimmed(s.Get("text").AsString());
        seg.startSec = s.Get("start").AsDouble();
        seg.endSec   = s.Get("end").AsDouble();
        seg.speakerId = s.Get("speaker").IsNumber() ? s.Get("speaker").AsInt() : -1;
        for (const auto& w : s.Get("words").AsArray()) {
            WordTiming wt;
            wt.word     = Trimmed(w.Get("word").AsString());
            wt.startSec = w.Get("start").AsDouble();
            wt.endSec   = w.Get("end").AsDouble();
            seg.words.push_back(std::move(wt));
        }
        res.segments.push_back(std::move(seg));
    }
    return res;
}

class LocalSpeechToText;

class LocalSttSession final : public ISttSession {
public:
    LocalSttSession(const SpeechToTextConfig& cfg, TranscriptionRequest req,
                    SttStreamCallback cb)
        : cfg_(cfg), req_(std::move(req)), cb_(std::move(cb)) {}

    Error PushAudio(const std::uint8_t* data, std::size_t size) override {
        if (!data && size) return Error::Make(ErrorCode::InvalidArgument, "null audio");
        audio_.insert(audio_.end(), data, data + size);
        return {};
    }

    TranscriptionResult Finish() override;

private:
    SpeechToTextConfig        cfg_;
    TranscriptionRequest      req_;
    SttStreamCallback         cb_;
    std::vector<std::uint8_t> audio_;
};

class LocalSpeechToText final : public LocalBase<SpeechToTextConfig, ISpeechToText> {
public:
    using LocalBase::LocalBase;

    TranscriptionResult Transcribe(const TranscriptionRequest& request) override {
        TranscriptionResult res;
        if (request.audio.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "audio is empty");
            return res;
        }
        const std::string boundary = "UltraAI-multipart-2f9c1e";
        const std::string mime =
            request.audio.mimeType.empty() ? "audio/wav" : request.audio.mimeType;

        std::string body;
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"file\"; filename=\"" +
                AudioFileName(mime) + "\"\r\n";
        body += "Content-Type: " + mime + "\r\n\r\n";
        body.append(reinterpret_cast<const char*>(request.audio.data.data()),
                    request.audio.data.size());
        body += "\r\n";
        if (!cfg_.model.empty()) MultipartField(body, boundary, "model", cfg_.model);
        MultipartField(body, boundary, "response_format", "verbose_json");
        if (!request.languageHint.empty())
            MultipartField(body, boundary, "language", request.languageHint);
        if (request.enableWordTimestamps) {
            MultipartField(body, boundary, "timestamp_granularities[]", "word");
            MultipartField(body, boundary, "timestamp_granularities[]", "segment");
        }
        body += "--" + boundary + "--\r\n";

        localhttp::HttpRequest http =
            MakeRequest(cfg_, "/v1/audio/transcriptions", std::move(body));
        http.contentType = "multipart/form-data; boundary=" + boundary;

        localhttp::HttpResponse resp;
        res.error = localhttp::Fetch(http, &resp);
        if (res.error) return res;
        res.error = localhttp::MapHttpStatus(resp.status, resp.body);
        if (res.error) return res;

        Value json;
        std::string parseErr;
        if (!Value::Parse(resp.body, &json, &parseErr)) {
            res.error = Error::Make(ErrorCode::Internal,
                                    "malformed JSON from server: " + parseErr);
            return res;
        }
        res = ParseTranscription(json);
        if (res.detectedLanguage.empty()) res.detectedLanguage = request.languageHint;
        return res;
    }

    // Local servers have no live-transcription endpoint: the session buffers
    // audio and transcribes once on Finish(), emitting FinalSegment events.
    std::unique_ptr<ISttSession> StartStream(const TranscriptionRequest& request,
                                             SttStreamCallback onEvent,
                                             Error* error) override {
        if (error) *error = Error::None_();
        return std::unique_ptr<ISttSession>(
            new LocalSttSession(cfg_, request, std::move(onEvent)));
    }
};

TranscriptionResult LocalSttSession::Finish() {
    TranscriptionRequest req = req_;
    req.audio.data = std::move(audio_);
    if (req.audio.mimeType.empty()) req.audio.mimeType = "audio/wav";
    LocalSpeechToText stt(cfg_);
    TranscriptionResult res = stt.Transcribe(req);
    if (cb_) {
        if (res.error) {
            SttStreamEvent ev;
            ev.kind = SttStreamEventKind::Error;
            ev.error = res.error;
            cb_(ev);
        } else {
            for (const auto& seg : res.segments) {
                SttStreamEvent ev;
                ev.kind    = SttStreamEventKind::FinalSegment;
                ev.segment = seg;
                cb_(ev);
            }
        }
    }
    return res;
}

// ---------------------------------------------------------------- TextToSpeech

static std::string TtsFormatFromMime(const std::string& mime) {
    if (mime == "audio/mpeg" || mime == "audio/mp3") return "mp3";
    if (mime == "audio/ogg" || mime == "audio/opus") return "opus";
    if (mime == "audio/flac")                        return "flac";
    return "wav";
}

class LocalTextToSpeech final : public LocalBase<TextToSpeechConfig, ITextToSpeech> {
public:
    using LocalBase::LocalBase;

    std::vector<Voice> ListVoices(Error* error) override {
        localhttp::HttpRequest http = MakeRequest(cfg_, "/v1/audio/voices", "", "GET");
        localhttp::HttpResponse resp;
        Error err = localhttp::Fetch(http, &resp);
        if (!err && resp.status == 404)
            err = Error::Make(ErrorCode::NotSupported,
                              "this server does not expose /v1/audio/voices");
        if (!err) err = localhttp::MapHttpStatus(resp.status, resp.body);
        if (err) { if (error) *error = err; return {}; }

        std::vector<Voice> voices;
        Value json;
        if (Value::Parse(resp.body, &json)) {
            const Value& list = json.IsArray() ? json : json.Get("voices");
            for (const auto& v : list.AsArray()) {
                Voice voice;
                voice.id = v.Get("id").AsString();
                if (voice.id.empty()) voice.id = v.Get("voice_id").AsString();
                if (voice.id.empty()) voice.id = v.Get("name").AsString();
                voice.name        = v.Get("name").AsString();
                voice.language    = v.Get("language").AsString();
                voice.gender      = v.Get("gender").AsString();
                voice.description = v.Get("description").AsString();
                voices.push_back(std::move(voice));
            }
        }
        if (error) *error = Error::None_();
        return voices;
    }

    SpeakResult Speak(const SpeakRequest& request) override {
        SpeakResult res;
        if (request.text.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "text is empty");
            return res;
        }
        Value body = Value::MakeObject();
        if (!cfg_.model.empty()) body.Set("model", cfg_.model);
        body.Set("input", request.text);
        if (!request.voiceId.empty()) body.Set("voice", request.voiceId);
        body.Set("response_format", TtsFormatFromMime(request.audioFormat));
        if (request.speed != 1.0) body.Set("speed", request.speed);

        localhttp::HttpRequest http =
            MakeRequest(cfg_, "/v1/audio/speech", body.Dump());
        localhttp::HttpResponse resp;
        res.error = localhttp::Fetch(http, &resp);
        if (res.error) return res;
        res.error = localhttp::MapHttpStatus(resp.status, resp.body);
        if (res.error) return res;

        res.audio.data.assign(resp.body.begin(), resp.body.end());
        auto ct = resp.headers.find("content-type");
        res.audio.mimeType = ct != resp.headers.end() ? ct->second : request.audioFormat;
        return res;
    }

    Error SpeakStream(const SpeakRequest& request, TtsChunkCallback onChunk) override {
        if (!onChunk) return Error::Make(ErrorCode::InvalidArgument, "null callback");
        SpeakResult full = Speak(request); // synthesized fully, then chunked
        if (full.error) return full.error;
        const auto& bytes = full.audio.data;
        const std::size_t chunkSize = std::max<std::size_t>(1, bytes.size() / 3);
        for (std::size_t off = 0; off < bytes.size(); off += chunkSize) {
            MediaBlob chunk;
            chunk.mimeType = full.audio.mimeType;
            const std::size_t n = std::min(chunkSize, bytes.size() - off);
            chunk.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(off),
                              bytes.begin() + static_cast<std::ptrdiff_t>(off + n));
            onChunk(chunk, off + n >= bytes.size());
        }
        return {};
    }

    Voice CloneVoice(const std::string& /*name*/,
                     const std::vector<MediaBlob>& /*samples*/,
                     Error* error) override {
        if (error) *error = Error::Make(ErrorCode::NotSupported,
                                        "voice cloning is not part of the local server API");
        return {};
    }
};

// ---------------------------------------------------------------- ImageGen

class LocalImageGen final : public LocalBase<ImageGenConfig, IImageGen> {
public:
    using LocalBase::LocalBase;

    ImageGenResult Generate(const ImageGenRequest& request,
                            JobProgressCallback onProgress) override {
        ImageGenResult res;
        if (request.mode != ImageGenMode::TextToImage) {
            res.error = Error::Make(ErrorCode::NotSupported,
                                    "local adapter currently supports TextToImage only");
            return res;
        }
        Value body = Value::MakeObject();
        if (!cfg_.model.empty()) body.Set("model", cfg_.model);
        body.Set("prompt", request.prompt);
        body.Set("n", std::max(1, request.count));
        body.Set("size", std::to_string(request.width) + "x" + std::to_string(request.height));
        body.Set("response_format", "b64_json");

        Value json;
        res.error = PostJson(cfg_, "/v1/images/generations", body, &json);
        if (res.error) return res;

        for (const auto& item : json.Get("data").AsArray()) {
            std::vector<std::uint8_t> bytes;
            if (!base64::Decode(item.Get("b64_json").AsString(), &bytes)) {
                res.error = Error::Make(ErrorCode::Internal, "invalid base64 image data");
                return res;
            }
            MediaBlob img;
            img.data     = std::move(bytes);
            img.mimeType = "image/png";
            res.images.push_back(std::move(img));
        }
        if (res.images.empty()) {
            res.error = Error::Make(ErrorCode::Internal, "server returned no images");
            return res;
        }
        if (onProgress) onProgress(1.0, "done"); // API has no incremental progress
        return res;
    }
};

// ---------------------------------------------------------------- VisionAnalyzer

class LocalVisionAnalyzer final : public LocalBase<VisionAnalyzerConfig, IVisionAnalyzer> {
public:
    using LocalBase::LocalBase;

    AnalyzeResult Analyze(const AnalyzeRequest& request) override {
        AnalyzeResult res;
        if (request.image.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "image is empty");
            return res;
        }
        for (VisionTask task : request.tasks) {
            std::string reply;
            switch (task) {
                case VisionTask::Caption:
                    res.error = SimpleChat(cfg_, "You are an image captioning engine.",
                                           "Describe this image in one concise sentence.",
                                           &request.image, &reply);
                    res.caption = Trimmed(reply);
                    break;
                case VisionTask::Tags: {
                    res.error = SimpleChat(cfg_, "You are an image tagging engine.",
                                           "List up to 8 short lowercase tags for this image, "
                                           "comma-separated, no other text.",
                                           &request.image, &reply);
                    std::istringstream ss(reply);
                    std::string tag;
                    while (std::getline(ss, tag, ',')) {
                        tag = Trimmed(tag);
                        if (!tag.empty()) res.tags.push_back({ tag, 1.0 });
                    }
                    break;
                }
                case VisionTask::OCR:
                    res.error = SimpleChat(cfg_, "You are an OCR engine.",
                                           "Transcribe all text visible in this image. "
                                           "Output only the transcription.",
                                           &request.image, &reply);
                    res.ocrText = Trimmed(reply);
                    break;
                case VisionTask::VQA:
                    res.error = SimpleChat(cfg_, "Answer the question about the image concisely.",
                                           request.question, &request.image, &reply);
                    res.answer = Trimmed(reply);
                    break;
                default:
                    res.error = Error::Make(
                        ErrorCode::NotSupported,
                        "local adapter supports Caption/Tags/OCR/VQA vision tasks only");
                    break;
            }
            if (res.error) return res;
        }
        return res;
    }
};

// ---------------------------------------------------------------- Translator

static const char* FormalityHint(Formality f) {
    switch (f) {
        case Formality::Formal:   return " Use a formal register.";
        case Formality::Informal: return " Use an informal register.";
        case Formality::Default:  break;
    }
    return "";
}

class LocalTranslator final : public LocalBase<TranslatorConfig, ITranslator> {
public:
    using LocalBase::LocalBase;

    TranslateResult Translate(const TranslateRequest& request) override {
        TranslateResult res;
        if (request.targetLanguage.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "targetLanguage required");
            return res;
        }
        std::string system = "You are a translation engine. Translate the user's text ";
        system += request.sourceLanguage.empty()
            ? "from its original language"
            : "from " + request.sourceLanguage;
        system += " to " + request.targetLanguage + ".";
        system += FormalityHint(request.formality);
        for (const auto& kv : request.glossary)
            system += " Always translate \"" + kv.first + "\" as \"" + kv.second + "\".";
        system += " Output only the translated text.";

        for (const auto& text : request.texts) {
            std::string reply;
            res.error = SimpleChat(cfg_, system, text, nullptr, &reply);
            if (res.error) return res;
            Translation t;
            t.text = Trimmed(reply);
            t.detectedSourceLanguage = request.sourceLanguage;
            res.translations.push_back(std::move(t));
        }
        return res;
    }

    LanguageDetection DetectLanguage(const std::string& text) override {
        LanguageDetection d;
        if (text.empty()) {
            d.error = Error::Make(ErrorCode::InvalidArgument, "text is empty");
            return d;
        }
        std::string reply;
        d.error = SimpleChat(cfg_,
                             "Identify the language of the user's text. "
                             "Reply with only its BCP-47 language code.",
                             text, nullptr, &reply);
        if (d.error) return d;
        d.language   = Trimmed(reply);
        d.confidence = d.language.empty() ? 0.0 : 0.9; // LLM-backed: no true score
        return d;
    }
};

// ---------------------------------------------------------------- CodeAssist

static FindingSeverity MapSeverity(const std::string& s) {
    if (s == "error")   return FindingSeverity::Error;
    if (s == "info")    return FindingSeverity::Info;
    return FindingSeverity::Warning;
}

class LocalCodeAssist final : public LocalBase<CodeAssistConfig, ICodeAssist> {
public:
    using LocalBase::LocalBase;

    CodeResult Run(const CodeRequest& request) override {
        CodeResult res;
        std::string system, user;
        switch (request.task) {
            case CodeTask::Generate:
                system = "You are a code generator. Output only " + Lang(request) + " code.";
                user   = request.instruction;
                break;
            case CodeTask::Explain:
                system = "Explain what the following code does, concisely.";
                user   = request.code;
                break;
            case CodeTask::Refactor:
                system = "Refactor the following " + Lang(request) + " code as instructed. "
                         "Output only the refactored code. Instruction: " + request.instruction;
                user   = request.code;
                break;
            case CodeTask::TranslateLanguage:
                if (request.targetLanguage.empty()) {
                    res.error = Error::Make(ErrorCode::InvalidArgument,
                                            "targetLanguage required");
                    return res;
                }
                system = "Port the following " + Lang(request) + " code to " +
                         request.targetLanguage + ". Output only the ported code.";
                user   = request.code;
                break;
            case CodeTask::DetectBugs:
                system = "You are a static analyzer. Report bugs in the user's code as JSON: "
                         "{\"findings\":[{\"line\":<1-based int>,\"severity\":"
                         "\"info|warning|error\",\"message\":\"...\",\"suggestion\":\"...\"}]}. "
                         "Output only the JSON.";
                user   = request.code;
                break;
            case CodeTask::GenerateTests:
                system = "Write unit tests for the following " + Lang(request) +
                         " code. Output only the test code.";
                user   = request.code;
                break;
            case CodeTask::GenerateDocs:
                system = "Add documentation comments to the following " + Lang(request) +
                         " code. Output only the documented code.";
                user   = request.code;
                break;
            case CodeTask::FillInMiddle:
                system = "Complete the code between PREFIX and SUFFIX. "
                         "Output only the middle part, no markers.";
                user   = "PREFIX:\n" + request.prefix + "\nSUFFIX:\n" + request.suffix;
                break;
        }

        ChatRequest chat;
        Message sys; sys.role = Role::System; sys.text = system;
        Message usr; usr.role = Role::User;   usr.text = user;
        chat.messages.push_back(std::move(sys));
        chat.messages.push_back(std::move(usr));
        ChatResponse resp;
        res.error = ChatOnce(cfg_, chat, &resp);
        if (res.error) return res;
        res.usage = resp.usage;

        std::string text = StripCodeFence(Trimmed(resp.text));
        if (request.task == CodeTask::DetectBugs) {
            Value json;
            if (Value::Parse(text, &json)) {
                for (const auto& f : json.Get("findings").AsArray()) {
                    CodeFinding finding;
                    finding.line       = f.Get("line").AsInt();
                    finding.severity   = MapSeverity(f.Get("severity").AsString());
                    finding.message    = f.Get("message").AsString();
                    finding.suggestion = f.Get("suggestion").AsString();
                    res.findings.push_back(std::move(finding));
                }
                res.output = std::to_string(res.findings.size()) + " finding(s)";
                return res;
            }
            // Model ignored the JSON instruction — surface its text instead.
        }
        if (request.task == CodeTask::FillInMiddle)
            text = request.prefix + text + request.suffix;
        res.output = text;
        return res;
    }

private:
    static std::string Lang(const CodeRequest& r) {
        return r.language.empty() ? "source" : r.language;
    }
};

// ---------------------------------------------------------------- registration

} // namespace local

namespace detail {

void RegisterLocalAdapters() {
    using namespace local;
    RegisterTextLLMProvider("local",
        [](const TextLLMConfig& c) { return std::unique_ptr<ITextLLM>(new LocalTextLLM(c)); });
    RegisterEmbeddingsProvider("local",
        [](const EmbeddingsConfig& c) { return std::unique_ptr<IEmbeddings>(new LocalEmbeddings(c)); });
    RegisterSpeechToTextProvider("local",
        [](const SpeechToTextConfig& c) { return std::unique_ptr<ISpeechToText>(new LocalSpeechToText(c)); });
    RegisterTextToSpeechProvider("local",
        [](const TextToSpeechConfig& c) { return std::unique_ptr<ITextToSpeech>(new LocalTextToSpeech(c)); });
    RegisterImageGenProvider("local",
        [](const ImageGenConfig& c) { return std::unique_ptr<IImageGen>(new LocalImageGen(c)); });
    RegisterVisionAnalyzerProvider("local",
        [](const VisionAnalyzerConfig& c) { return std::unique_ptr<IVisionAnalyzer>(new LocalVisionAnalyzer(c)); });
    RegisterTranslatorProvider("local",
        [](const TranslatorConfig& c) { return std::unique_ptr<ITranslator>(new LocalTranslator(c)); });
    RegisterCodeAssistProvider("local",
        [](const CodeAssistConfig& c) { return std::unique_ptr<ICodeAssist>(new LocalCodeAssist(c)); });
    // No VideoGen / MusicGen: no settled local-server API exists for them yet.
}

} // namespace detail
} // namespace UltraAI
