// TestLocalAdapters.cpp — provider "local" against an in-process fake
// OpenAI-compatible server (no network beyond loopback, no real models).
#include "FakeLocalServer.h"
#include "TestHelpers.h"
#include <UltraAI.h>

using namespace UltraAI;
using testserver::FakeLocalServer;
using testserver::Request;
using testserver::Response;

static FakeLocalServer& Server() {
    static FakeLocalServer server;
    return server;
}

template <typename ConfigT>
static ConfigT LocalCfg() {
    ConfigT cfg;
    cfg.providerId = "local";
    cfg.endpoint   = Server().BaseUrl();
    return cfg;
}

// ---------------------------------------------------------------- registration

ULTRAAI_TEST(LocalIsRegisteredForEightCapabilities) {
    auto has_local = [](const std::vector<std::string>& v) {
        for (const auto& s : v) if (s == "local") return true;
        return false;
    };
    ULTRAAI_CHECK(has_local(ListTextLLMProviders()));
    ULTRAAI_CHECK(has_local(ListEmbeddingsProviders()));
    ULTRAAI_CHECK(has_local(ListSpeechToTextProviders()));
    ULTRAAI_CHECK(has_local(ListTextToSpeechProviders()));
    ULTRAAI_CHECK(has_local(ListImageGenProviders()));
    ULTRAAI_CHECK(has_local(ListVisionAnalyzerProviders()));
    ULTRAAI_CHECK(has_local(ListTranslatorProviders()));
    ULTRAAI_CHECK(has_local(ListCodeAssistProviders()));
    // No local video/music generation server API exists yet:
    ULTRAAI_CHECK(!has_local(ListVideoGenProviders()));
    ULTRAAI_CHECK(!has_local(ListMusicGenProviders()));
    // Mock stays the default provider:
    Error err;
    auto llm = CreateTextLLM(TextLLMConfig{}, &err);
    ULTRAAI_CHECK(llm && llm->ProviderId() == "mock");
}

// ---------------------------------------------------------------- TextLLM

ULTRAAI_TEST(LocalChatBasic) {
    Server().Handle("/v1/chat/completions", [](const Request& req) {
        Response resp;
        // Echo-check: the adapter must have sent OpenAI-shaped JSON.
        ULTRAAI_CHECK(req.body.find("\"messages\"") != std::string::npos);
        ULTRAAI_CHECK(req.body.find("summarize a kettle") != std::string::npos);
        resp.body = R"({
            "choices":[{"message":{"role":"assistant","content":"a kettle boils water"},
                        "finish_reason":"stop"}],
            "usage":{"prompt_tokens":7,"completion_tokens":5,"total_tokens":12}
        })";
        return resp;
    });
    auto llm = CreateTextLLM(LocalCfg<TextLLMConfig>());
    ULTRAAI_CHECK(llm && llm->ProviderId() == "local");

    ChatRequest req;
    Message m; m.role = Role::User; m.text = "summarize a kettle";
    req.messages.push_back(std::move(m));
    ChatResponse resp = llm->Chat(req);
    ULTRAAI_CHECK(!resp.error);
    ULTRAAI_CHECK(resp.text == "a kettle boils water");
    ULTRAAI_CHECK(resp.finishReason == FinishReason::Stop);
    ULTRAAI_CHECK(resp.usage.inputTokens == 7);
    ULTRAAI_CHECK(resp.usage.totalTokens == 12);
}

ULTRAAI_TEST(LocalChatToolCalls) {
    Server().Handle("/v1/chat/completions", [](const Request& req) {
        ULTRAAI_CHECK(req.body.find("\"tools\"") != std::string::npos);
        ULTRAAI_CHECK(req.body.find("get_weather") != std::string::npos);
        Response resp;
        resp.body = R"({
            "choices":[{"message":{"role":"assistant","content":null,
                "tool_calls":[{"id":"call-1","type":"function",
                    "function":{"name":"get_weather","arguments":"{\"city\":\"Berlin\"}"}}]},
                "finish_reason":"tool_calls"}],
            "usage":{"prompt_tokens":9,"completion_tokens":4,"total_tokens":13}
        })";
        return resp;
    });
    auto llm = CreateTextLLM(LocalCfg<TextLLMConfig>());
    ChatRequest req;
    Message m; m.text = "weather in berlin?"; req.messages.push_back(m);
    ToolSpec tool;
    tool.name = "get_weather";
    tool.parametersJsonSchema = R"({"type":"object"})";
    req.tools.push_back(tool);

    ChatResponse resp = llm->Chat(req);
    ULTRAAI_CHECK(!resp.error);
    ULTRAAI_CHECK(resp.finishReason == FinishReason::ToolUse);
    ULTRAAI_CHECK(resp.toolCalls.size() == 1);
    ULTRAAI_CHECK(resp.toolCalls[0].id == "call-1");
    ULTRAAI_CHECK(resp.toolCalls[0].name == "get_weather");
    ULTRAAI_CHECK(resp.toolCalls[0].argumentsJson.find("Berlin") != std::string::npos);
}

ULTRAAI_TEST(LocalChatStreamReassembles) {
    Server().Handle("/v1/chat/completions", [](const Request& req) {
        ULTRAAI_CHECK(req.body.find("\"stream\":true") != std::string::npos);
        Response resp;
        resp.contentType = "text/event-stream";
        resp.body =
            "data: {\"choices\":[{\"delta\":{\"content\":\"one\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\" two\"}}]}\n\n"
            "data: {\"choices\":[{\"delta\":{\"content\":\" three\"}}]}\n\n"
            "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":3,"
            "\"completion_tokens\":3,\"total_tokens\":6}}\n\n"
            "data: [DONE]\n\n";
        return resp;
    });
    auto llm = CreateTextLLM(LocalCfg<TextLLMConfig>());
    ChatRequest req;
    Message m; m.text = "count"; req.messages.push_back(m);

    std::string assembled;
    bool done = false;
    TokenUsage usage;
    Error err = llm->ChatStream(req, [&](const StreamEvent& ev) {
        if (ev.kind == StreamEventKind::TextDelta) assembled += ev.textDelta;
        if (ev.kind == StreamEventKind::Done) { done = true; usage = ev.usage; }
    });
    ULTRAAI_CHECK(!err);
    ULTRAAI_CHECK(done);
    ULTRAAI_CHECK(assembled == "one two three");
    ULTRAAI_CHECK(usage.totalTokens == 6);
}

// ---------------------------------------------------------------- error mapping

ULTRAAI_TEST(LocalErrorMapping) {
    auto llm = CreateTextLLM(LocalCfg<TextLLMConfig>());
    ChatRequest req;
    Message m; m.text = "x"; req.messages.push_back(m);

    Server().Handle("/v1/chat/completions",
                    [](const Request&) { Response r; r.status = 401; r.body = "no key"; return r; });
    ULTRAAI_CHECK(llm->Chat(req).error.code == ErrorCode::AuthFailure);

    Server().Handle("/v1/chat/completions",
                    [](const Request&) { Response r; r.status = 429; return r; });
    ULTRAAI_CHECK(llm->Chat(req).error.code == ErrorCode::RateLimited);

    Server().Handle("/v1/chat/completions",
                    [](const Request&) { Response r; r.status = 500; return r; });
    ULTRAAI_CHECK(llm->Chat(req).error.code == ErrorCode::Internal);

    Server().Handle("/v1/chat/completions",
                    [](const Request&) { Response r; r.status = 400; return r; });
    ULTRAAI_CHECK(llm->Chat(req).error.code == ErrorCode::InvalidArgument);
}

ULTRAAI_TEST(LocalTransportRefusesNonLoopback) {
    TextLLMConfig cfg;
    cfg.providerId = "local";
    cfg.endpoint   = "http://example.com:80";
    auto llm = CreateTextLLM(cfg);
    ChatRequest req;
    Message m; m.text = "x"; req.messages.push_back(m);
    ChatResponse resp = llm->Chat(req);
    ULTRAAI_CHECK(resp.error.code == ErrorCode::Network);
    ULTRAAI_CHECK(resp.error.message.find("loopback") != std::string::npos);
}

ULTRAAI_TEST(LocalConnectionRefusedIsNetwork) {
    // Grab an ephemeral port and release it: connecting should now fail fast.
    int freePort;
    {
        FakeLocalServer probe;
        freePort = probe.port();
    }
    TextLLMConfig cfg;
    cfg.providerId = "local";
    cfg.endpoint   = "http://127.0.0.1:" + std::to_string(freePort);
    auto llm = CreateTextLLM(cfg);
    ChatRequest req;
    Message m; m.text = "x"; req.messages.push_back(m);
    ULTRAAI_CHECK(llm->Chat(req).error.code == ErrorCode::Network);
}

// ---------------------------------------------------------------- Embeddings

ULTRAAI_TEST(LocalEmbeddingsBatch) {
    Server().Handle("/v1/embeddings", [](const Request& req) {
        ULTRAAI_CHECK(req.body.find("\"input\"") != std::string::npos);
        Response resp;
        resp.body = R"({
            "data":[{"embedding":[0.1,0.2,0.3]},{"embedding":[0.4,0.5,0.6]}],
            "usage":{"prompt_tokens":4,"total_tokens":4}
        })";
        return resp;
    });
    auto emb = CreateEmbeddings(LocalCfg<EmbeddingsConfig>());
    EmbeddingsRequest req;
    req.texts = { "kettle", "teapot" };
    EmbeddingsResponse resp = emb->Embed(req);
    ULTRAAI_CHECK(!resp.error);
    ULTRAAI_CHECK(resp.vectors.size() == 2);
    ULTRAAI_CHECK(resp.vectors[0].size() == 3);
    ULTRAAI_CHECK(resp.vectors[1][2] > 0.59f && resp.vectors[1][2] < 0.61f);
    ULTRAAI_CHECK(resp.usage.inputTokens == 4);
}

// ---------------------------------------------------------------- SpeechToText

ULTRAAI_TEST(LocalTranscribeMultipart) {
    Server().Handle("/v1/audio/transcriptions", [](const Request& req) {
        ULTRAAI_CHECK(req.headers.find("multipart/form-data") != std::string::npos);
        ULTRAAI_CHECK(req.body.find("PCMBYTES") != std::string::npos);       // audio bytes
        ULTRAAI_CHECK(req.body.find("verbose_json") != std::string::npos);
        Response resp;
        resp.body = R"({
            "text":"hello ultra os","language":"en",
            "segments":[{"start":0.0,"end":1.5,"text":" hello ultra os",
                         "words":[{"word":"hello","start":0.0,"end":0.4},
                                  {"word":"ultra","start":0.5,"end":0.9},
                                  {"word":"os","start":1.0,"end":1.2}]}]
        })";
        return resp;
    });
    auto stt = CreateSpeechToText(LocalCfg<SpeechToTextConfig>());
    TranscriptionRequest req;
    req.audio = MediaBlob::FromText("PCMBYTES", "audio/wav");
    req.enableWordTimestamps = true;
    TranscriptionResult res = stt->Transcribe(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.text == "hello ultra os");
    ULTRAAI_CHECK(res.detectedLanguage == "en");
    ULTRAAI_CHECK(res.segments.size() == 1);
    ULTRAAI_CHECK(res.segments[0].text == "hello ultra os"); // trimmed
    ULTRAAI_CHECK(res.segments[0].words.size() == 3);
    ULTRAAI_CHECK(res.segments[0].words[2].word == "os");
}

ULTRAAI_TEST(LocalSttSessionBuffersAndFinishes) {
    Server().Handle("/v1/audio/transcriptions", [](const Request& req) {
        ULTRAAI_CHECK(req.body.find("AABB") != std::string::npos);
        Response resp;
        resp.body = R"({"text":"buffered","segments":[{"start":0,"end":1,"text":"buffered"}]})";
        return resp;
    });
    auto stt = CreateSpeechToText(LocalCfg<SpeechToTextConfig>());
    bool finalSeen = false;
    Error err;
    auto session = stt->StartStream(TranscriptionRequest{}, [&](const SttStreamEvent& ev) {
        if (ev.kind == SttStreamEventKind::FinalSegment) finalSeen = true;
    }, &err);
    ULTRAAI_CHECK(session && !err);
    const std::uint8_t chunk[] = { 'A', 'A', 'B', 'B' };
    ULTRAAI_CHECK(!session->PushAudio(chunk, sizeof(chunk)));
    TranscriptionResult res = session->Finish();
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.text == "buffered");
    ULTRAAI_CHECK(finalSeen);
}

// ---------------------------------------------------------------- TextToSpeech

ULTRAAI_TEST(LocalSpeakBinaryAndStream) {
    Server().Handle("/v1/audio/speech", [](const Request& req) {
        ULTRAAI_CHECK(req.body.find("\"input\":\"welcome\"") != std::string::npos);
        ULTRAAI_CHECK(req.body.find("\"response_format\":\"wav\"") != std::string::npos);
        Response resp;
        resp.contentType = "audio/wav";
        resp.body = "RIFFFAKEWAVDATA";
        return resp;
    });
    auto tts = CreateTextToSpeech(LocalCfg<TextToSpeechConfig>());
    SpeakRequest req;
    req.text = "welcome";
    SpeakResult res = tts->Speak(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.audio.AsText() == "RIFFFAKEWAVDATA");
    ULTRAAI_CHECK(res.audio.mimeType == "audio/wav");

    std::vector<std::uint8_t> assembled;
    bool sawLast = false;
    Error err = tts->SpeakStream(req, [&](const MediaBlob& chunk, bool last) {
        assembled.insert(assembled.end(), chunk.data.begin(), chunk.data.end());
        if (last) sawLast = true;
    });
    ULTRAAI_CHECK(!err);
    ULTRAAI_CHECK(sawLast);
    ULTRAAI_CHECK(assembled == res.audio.data);

    ULTRAAI_CHECK(tts->Speak(SpeakRequest{}).error.code == ErrorCode::InvalidArgument);
    Error cloneErr;
    tts->CloneVoice("X", {}, &cloneErr);
    ULTRAAI_CHECK(cloneErr.code == ErrorCode::NotSupported);
}

ULTRAAI_TEST(LocalListVoicesOptionalEndpoint) {
    Server().Handle("/v1/audio/voices", [](const Request&) {
        Response resp;
        resp.body = R"({"voices":[{"id":"alto","name":"Alto","language":"en-US"}]})";
        return resp;
    });
    auto tts = CreateTextToSpeech(LocalCfg<TextToSpeechConfig>());
    Error err;
    auto voices = tts->ListVoices(&err);
    ULTRAAI_CHECK(!err);
    ULTRAAI_CHECK(voices.size() == 1);
    ULTRAAI_CHECK(voices[0].id == "alto");
    ULTRAAI_CHECK(voices[0].language == "en-US");
}

// ---------------------------------------------------------------- ImageGen

ULTRAAI_TEST(LocalImageGenB64) {
    Server().Handle("/v1/images/generations", [](const Request& req) {
        ULTRAAI_CHECK(req.body.find("\"size\":\"512x512\"") != std::string::npos);
        Response resp;
        // "PNGDATA" -> UE5HREFUQQ==
        resp.body = R"({"data":[{"b64_json":"UE5HREFUQQ=="}]})";
        return resp;
    });
    auto ig = CreateImageGen(LocalCfg<ImageGenConfig>());
    ImageGenRequest req;
    req.prompt = "a kettle";
    req.width = 512; req.height = 512;
    double lastFraction = 0.0;
    ImageGenResult res = ig->Generate(req, [&](double f, const std::string&) {
        lastFraction = f;
    });
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.images.size() == 1);
    ULTRAAI_CHECK(res.images[0].AsText() == "PNGDATA");
    ULTRAAI_CHECK(lastFraction == 1.0);

    ImageGenRequest inpaint;
    inpaint.mode = ImageGenMode::Inpaint;
    ULTRAAI_CHECK(ig->Generate(inpaint).error.code == ErrorCode::NotSupported);
}

// ---------------------------------------------------------------- VisionAnalyzer

ULTRAAI_TEST(LocalVisionCaptionAndVqa) {
    Server().Handle("/v1/chat/completions", [](const Request& req) {
        // The adapter must inline the image as a data: URI content part.
        ULTRAAI_CHECK(req.body.find("data:image/png;base64,") != std::string::npos);
        Response resp;
        resp.body = R"({"choices":[{"message":{"content":" a red kettle "},
                                    "finish_reason":"stop"}]})";
        return resp;
    });
    auto va = CreateVisionAnalyzer(LocalCfg<VisionAnalyzerConfig>());
    AnalyzeRequest req;
    req.image = MediaBlob::FromText("PNGDATA", "image/png");
    req.tasks = { VisionTask::Caption, VisionTask::VQA };
    req.question = "what is this?";
    AnalyzeResult res = va->Analyze(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.caption == "a red kettle"); // trimmed
    ULTRAAI_CHECK(res.answer  == "a red kettle");

    AnalyzeRequest faces;
    faces.image = req.image;
    faces.tasks = { VisionTask::Faces };
    ULTRAAI_CHECK(va->Analyze(faces).error.code == ErrorCode::NotSupported);
}

// ---------------------------------------------------------------- Translator

ULTRAAI_TEST(LocalTranslateGlossaryPromptAndBatch) {
    Server().Handle("/v1/chat/completions", [](const Request& req) {
        // Glossary and register directives must reach the system prompt.
        ULTRAAI_CHECK(req.body.find("Welt") != std::string::npos);
        ULTRAAI_CHECK(req.body.find("formal register") != std::string::npos);
        Response resp;
        resp.body = R"({"choices":[{"message":{"content":"Hallo Welt"},
                                    "finish_reason":"stop"}]})";
        return resp;
    });
    auto tr = CreateTranslator(LocalCfg<TranslatorConfig>());
    TranslateRequest req;
    req.texts = { "hello world", "goodbye world" };
    req.targetLanguage = "de";
    req.formality = Formality::Formal;
    req.glossary["world"] = "Welt";
    TranslateResult res = tr->Translate(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.translations.size() == 2);
    ULTRAAI_CHECK(res.translations[0].text == "Hallo Welt");

    ULTRAAI_CHECK(tr->Translate(TranslateRequest{}).error.code ==
                  ErrorCode::InvalidArgument);
}

ULTRAAI_TEST(LocalDetectLanguage) {
    Server().Handle("/v1/chat/completions", [](const Request&) {
        Response resp;
        resp.body = R"({"choices":[{"message":{"content":"de"},"finish_reason":"stop"}]})";
        return resp;
    });
    auto tr = CreateTranslator(LocalCfg<TranslatorConfig>());
    LanguageDetection d = tr->DetectLanguage("Hallo Welt");
    ULTRAAI_CHECK(!d.error);
    ULTRAAI_CHECK(d.language == "de");
    ULTRAAI_CHECK(d.confidence > 0.0);
}

// ---------------------------------------------------------------- CodeAssist

ULTRAAI_TEST(LocalCodeAssistGenerateStripsFence) {
    Server().Handle("/v1/chat/completions", [](const Request&) {
        Response resp;
        resp.body = "{\"choices\":[{\"message\":{\"content\":"
                    "\"```cpp\\nint main() { return 0; }\\n```\"},"
                    "\"finish_reason\":\"stop\"}]}";
        return resp;
    });
    auto ca = CreateCodeAssist(LocalCfg<CodeAssistConfig>());
    CodeRequest req;
    req.task = CodeTask::Generate;
    req.language = "cpp";
    req.instruction = "hello world";
    CodeResult res = ca->Run(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.output == "int main() { return 0; }");
}

ULTRAAI_TEST(LocalCodeAssistDetectBugsJson) {
    Server().Handle("/v1/chat/completions", [](const Request&) {
        Response resp;
        resp.body = "{\"choices\":[{\"message\":{\"content\":"
                    "\"{\\\"findings\\\":[{\\\"line\\\":3,\\\"severity\\\":\\\"error\\\","
                    "\\\"message\\\":\\\"use after free\\\","
                    "\\\"suggestion\\\":\\\"reset the pointer\\\"}]}\"},"
                    "\"finish_reason\":\"stop\"}]}";
        return resp;
    });
    auto ca = CreateCodeAssist(LocalCfg<CodeAssistConfig>());
    CodeRequest req;
    req.task = CodeTask::DetectBugs;
    req.code = "int* p; delete p; *p = 1;";
    CodeResult res = ca->Run(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.findings.size() == 1);
    ULTRAAI_CHECK(res.findings[0].line == 3);
    ULTRAAI_CHECK(res.findings[0].severity == FindingSeverity::Error);
    ULTRAAI_CHECK(res.findings[0].message == "use after free");
}

ULTRAAI_TEST(LocalCodeAssistFillInMiddle) {
    Server().Handle("/v1/chat/completions", [](const Request&) {
        Response resp;
        resp.body = R"({"choices":[{"message":{"content":"return 42;"},
                                    "finish_reason":"stop"}]})";
        return resp;
    });
    auto ca = CreateCodeAssist(LocalCfg<CodeAssistConfig>());
    CodeRequest req;
    req.task = CodeTask::FillInMiddle;
    req.prefix = "int f() { ";
    req.suffix = " }";
    CodeResult res = ca->Run(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.output == "int f() { return 42; }");
}

int main() { std::printf("TestLocalAdapters: all assertions passed\n"); return 0; }
