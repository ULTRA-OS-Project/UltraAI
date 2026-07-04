// TestCore.cpp — common types, registry behaviour, factory routing.
#include "TestHelpers.h"
#include <UltraAI.h>

#include <cmath>
#include <cstring>

using namespace UltraAI;

ULTRAAI_TEST(ErrorBasics) {
    Error none;
    ULTRAAI_CHECK(!none);
    Error e = Error::Make(ErrorCode::Timeout, "took too long");
    ULTRAAI_CHECK(e);
    ULTRAAI_CHECK(e.code == ErrorCode::Timeout);
    ULTRAAI_CHECK(std::strcmp(ErrorCodeName(e.code), "Timeout") == 0);
    ULTRAAI_CHECK(std::strcmp(ErrorCodeName(ErrorCode::None), "None") == 0);
}

ULTRAAI_TEST(MediaBlobRoundTrip) {
    MediaBlob b = MediaBlob::FromText("hello", "text/plain");
    ULTRAAI_CHECK(!b.empty());
    ULTRAAI_CHECK(b.AsText() == "hello");
    ULTRAAI_CHECK(b.mimeType == "text/plain");
    MediaBlob byRef; byRef.uri = "file:///tmp/x.png";
    ULTRAAI_CHECK(!byRef.empty());
}

ULTRAAI_TEST(MockIsRegisteredForEveryCapability) {
    auto has_mock = [](const std::vector<std::string>& v) {
        for (const auto& s : v) if (s == "mock") return true;
        return false;
    };
    ULTRAAI_CHECK(has_mock(ListTextLLMProviders()));
    ULTRAAI_CHECK(has_mock(ListEmbeddingsProviders()));
    ULTRAAI_CHECK(has_mock(ListSpeechToTextProviders()));
    ULTRAAI_CHECK(has_mock(ListTextToSpeechProviders()));
    ULTRAAI_CHECK(has_mock(ListImageGenProviders()));
    ULTRAAI_CHECK(has_mock(ListVisionAnalyzerProviders()));
    ULTRAAI_CHECK(has_mock(ListTranslatorProviders()));
    ULTRAAI_CHECK(has_mock(ListVideoGenProviders()));
    ULTRAAI_CHECK(has_mock(ListMusicGenProviders()));
    ULTRAAI_CHECK(has_mock(ListCodeAssistProviders()));
}

ULTRAAI_TEST(DefaultRouteAndExplicitRoute) {
    Error err;
    TextLLMConfig byDefault;                 // providerId "" -> default
    auto a = CreateTextLLM(byDefault, &err);
    ULTRAAI_CHECK(a && !err);
    ULTRAAI_CHECK(a->ProviderId() == "mock");

    TextLLMConfig explicitId; explicitId.providerId = "mock";
    auto b = CreateTextLLM(explicitId, &err);
    ULTRAAI_CHECK(b && !err);
}

ULTRAAI_TEST(UnknownProviderFails) {
    Error err;
    TextLLMConfig cfg; cfg.providerId = "does-not-exist";
    auto llm = CreateTextLLM(cfg, &err);
    ULTRAAI_CHECK(!llm);
    ULTRAAI_CHECK(err.code == ErrorCode::ProviderUnavailable);
}

ULTRAAI_TEST(CustomProviderRegistration) {
    struct TinyLLM : ITextLLM {
        std::string ProviderId() const override { return "tiny"; }
        ChatResponse Chat(const ChatRequest&) override {
            ChatResponse r; r.text = "tiny"; r.finishReason = FinishReason::Stop; return r;
        }
        Error ChatStream(const ChatRequest&, StreamCallback cb) override {
            StreamEvent ev; ev.kind = StreamEventKind::Done; cb(ev); return {};
        }
    };
    ULTRAAI_CHECK(RegisterTextLLMProvider("tiny",
        [](const TextLLMConfig&) { return std::unique_ptr<ITextLLM>(new TinyLLM()); }));

    TextLLMConfig cfg; cfg.providerId = "tiny";
    Error err;
    auto llm = CreateTextLLM(cfg, &err);
    ULTRAAI_CHECK(llm && !err);
    ChatRequest req; Message m; m.text = "x"; req.messages.push_back(m);
    ULTRAAI_CHECK(llm->Chat(req).text == "tiny");
}

ULTRAAI_TEST(RawProviderEscapeHatch) {
    auto llm = CreateTextLLM(TextLLMConfig{});
    ULTRAAI_CHECK(llm);
    ULTRAAI_CHECK(llm->RawProvider() != nullptr); // mock exposes itself
}

ULTRAAI_TEST(MockFailOptionPropagates) {
    TextLLMConfig cfg;
    cfg.options["mock.fail"] = "simulated outage";
    auto llm = CreateTextLLM(cfg);
    ChatRequest req; Message m; m.text = "x"; req.messages.push_back(m);
    ChatResponse resp = llm->Chat(req);
    ULTRAAI_CHECK(resp.error);
    ULTRAAI_CHECK(resp.error.message == "simulated outage");
}

int main() { std::printf("TestCore: all assertions passed\n"); return 0; }
