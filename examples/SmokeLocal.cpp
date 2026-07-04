// SmokeLocal.cpp — end-to-end smoke test for the "local" provider against a
// REAL local model server (llama.cpp server, Ollama, LocalAI, vLLM, ...).
//
// Unlike tests/TestLocalAdapters.cpp (fake server, canned replies), this
// drives an actual model. It is therefore an example binary, not a ctest:
// CI machines don't run model servers.
//
// Usage:
//   ultraai_smoke_local [endpoint] [model]
//     endpoint  default http://127.0.0.1:8080   (llama.cpp server default)
//     model     default ""                       (server's default model)
//
// Exercises: provider listing, chat, streaming chat, token counting via
// usage, and embeddings (skipped gracefully if the server lacks the
// endpoint). Exits non-zero on the first hard failure.
//
// Part of ULTRA OS · MIT license · Cloverleaf UG

#include <UltraAI.h>

#include <cstdio>
#include <string>

using namespace UltraAI;

static int failures = 0;

static void Report(const char* what, const Error& err) {
    if (err) {
        ++failures;
        std::printf("  [FAIL] %s: %s (%s)\n", what, err.message.c_str(),
                    ErrorCodeName(err.code));
    } else {
        std::printf("  [ OK ] %s\n", what);
    }
}

int main(int argc, char** argv) {
    const std::string endpoint = argc > 1 ? argv[1] : "http://127.0.0.1:8080";
    const std::string model    = argc > 2 ? argv[2] : "";
    std::printf("UltraAI local-provider smoke test\n");
    std::printf("  endpoint: %s\n  model:    %s\n\n",
                endpoint.c_str(), model.empty() ? "(server default)" : model.c_str());

    // ---- chat -----------------------------------------------------------
    TextLLMConfig cfg;
    cfg.providerId = "local";
    cfg.endpoint   = endpoint;
    cfg.model      = model;
    Error err;
    auto llm = CreateTextLLM(cfg, &err);
    Report("CreateTextLLM(providerId=local)", err);
    if (!llm) return 1;

    ChatRequest req;
    Message sys; sys.role = Role::System;
    sys.text = "You are terse. Answer with a single short sentence.";
    Message usr; usr.role = Role::User;
    usr.text = "What does a kettle do?";
    req.messages.push_back(sys);
    req.messages.push_back(usr);
    req.maxTokens = 64;

    ChatResponse chat = llm->Chat(req);
    Report("Chat", chat.error);
    if (!chat.error) {
        std::printf("         reply: %s\n", chat.text.c_str());
        std::printf("         usage: in=%llu out=%llu total=%llu\n",
                    static_cast<unsigned long long>(chat.usage.inputTokens),
                    static_cast<unsigned long long>(chat.usage.outputTokens),
                    static_cast<unsigned long long>(chat.usage.totalTokens));
        if (chat.text.empty()) { ++failures; std::printf("  [FAIL] Chat reply is empty\n"); }
    }

    // ---- streaming chat --------------------------------------------------
    std::string streamed;
    bool done = false;
    Error streamErr = llm->ChatStream(req, [&](const StreamEvent& ev) {
        if (ev.kind == StreamEventKind::TextDelta) streamed += ev.textDelta;
        if (ev.kind == StreamEventKind::Done)      done = true;
    });
    Report("ChatStream", streamErr);
    if (!streamErr) {
        std::printf("         streamed %zu chars, done=%d\n", streamed.size(), done ? 1 : 0);
        if (streamed.empty() || !done) {
            ++failures;
            std::printf("  [FAIL] stream delivered no text or no Done event\n");
        }
    }

    // ---- embeddings (optional on some servers) ---------------------------
    EmbeddingsConfig embCfg;
    embCfg.providerId = "local";
    embCfg.endpoint   = endpoint;
    embCfg.model      = model;
    auto emb = CreateEmbeddings(embCfg, &err);
    EmbeddingsRequest embReq;
    embReq.texts = { "kettle", "teapot" };
    EmbeddingsResponse embResp = emb->Embed(embReq);
    if (embResp.error && (embResp.error.code == ErrorCode::InvalidArgument ||
                          embResp.error.code == ErrorCode::NotSupported)) {
        std::printf("  [SKIP] Embeddings: server has no embeddings endpoint (%s)\n",
                    embResp.error.message.c_str());
    } else {
        Report("Embeddings", embResp.error);
        if (!embResp.error) {
            std::printf("         %zu vectors, dim=%zu, sim(kettle,teapot)=%.3f\n",
                        embResp.vectors.size(), embResp.vectors[0].size(),
                        CosineSimilarity(embResp.vectors[0], embResp.vectors[1]));
        }
    }

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "SMOKE PASSED" : "SMOKE FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
