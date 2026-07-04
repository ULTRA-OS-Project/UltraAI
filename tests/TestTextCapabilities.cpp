// TestTextCapabilities.cpp — ITextLLM, IEmbeddings, ITranslator, ICodeAssist.
#include "TestHelpers.h"
#include <UltraAI.h>

#include <cmath>

using namespace UltraAI;

// ---------------------------------------------------------------- TextLLM

ULTRAAI_TEST(ChatEchoAndUsage) {
    auto llm = CreateTextLLM(TextLLMConfig{});
    ChatRequest req;
    Message m; m.role = Role::User; m.text = "summarize a kettle";
    req.messages.push_back(std::move(m));

    ChatResponse resp = llm->Chat(req);
    ULTRAAI_CHECK(!resp.error);
    ULTRAAI_CHECK(resp.text == "[mock] summarize a kettle");
    ULTRAAI_CHECK(resp.finishReason == FinishReason::Stop);
    ULTRAAI_CHECK(resp.usage.totalTokens ==
                  resp.usage.inputTokens + resp.usage.outputTokens);
    ULTRAAI_CHECK(llm->CountTokens(req) > 0);
}

ULTRAAI_TEST(ChatProgrammableReply) {
    TextLLMConfig cfg;
    cfg.options["mock.reply"] = "fixed answer";
    auto llm = CreateTextLLM(cfg);
    ChatRequest req; Message m; m.text = "anything"; req.messages.push_back(m);
    ULTRAAI_CHECK(llm->Chat(req).text == "fixed answer");
}

ULTRAAI_TEST(ChatStreamReassemblesFullText) {
    auto llm = CreateTextLLM(TextLLMConfig{});
    ChatRequest req; Message m; m.text = "one two three"; req.messages.push_back(m);

    std::string assembled;
    bool done = false;
    TokenUsage usage;
    Error err = llm->ChatStream(req, [&](const StreamEvent& ev) {
        if (ev.kind == StreamEventKind::TextDelta) assembled += ev.textDelta;
        if (ev.kind == StreamEventKind::Done) { done = true; usage = ev.usage; }
    });
    ULTRAAI_CHECK(!err);
    ULTRAAI_CHECK(done);
    ULTRAAI_CHECK(assembled == "[mock] one two three");
    ULTRAAI_CHECK(usage.totalTokens > 0);
}

ULTRAAI_TEST(ChatToolCallRoundTrip) {
    auto llm = CreateTextLLM(TextLLMConfig{});
    ChatRequest req;
    Message user; user.text = "what's the weather?"; req.messages.push_back(user);
    ToolSpec tool;
    tool.name = "get_weather";
    tool.parametersJsonSchema = R"({"type":"object"})";
    req.tools.push_back(tool);

    // 1st turn: model requests the tool.
    ChatResponse first = llm->Chat(req);
    ULTRAAI_CHECK(first.finishReason == FinishReason::ToolUse);
    ULTRAAI_CHECK(first.toolCalls.size() == 1);
    ULTRAAI_CHECK(first.toolCalls[0].name == "get_weather");

    // 2nd turn: feed the tool result back, get a final answer.
    Message assistant; assistant.role = Role::Assistant;
    assistant.toolCalls = first.toolCalls;
    req.messages.push_back(assistant);
    Message toolMsg; toolMsg.role = Role::Tool;
    toolMsg.toolCallId = first.toolCalls[0].id;
    toolMsg.text = "sunny, 21C";
    req.messages.push_back(toolMsg);

    ChatResponse second = llm->Chat(req);
    ULTRAAI_CHECK(!second.error);
    ULTRAAI_CHECK(second.finishReason == FinishReason::Stop);
    ULTRAAI_CHECK(!second.text.empty());
}

ULTRAAI_TEST(ChatStructuredOutput) {
    auto llm = CreateTextLLM(TextLLMConfig{});
    ChatRequest req;
    Message m; m.text = "kettle"; req.messages.push_back(m);
    req.responseJsonSchema = R"({"type":"object"})";
    ChatResponse resp = llm->Chat(req);
    ULTRAAI_CHECK(!resp.error);
    ULTRAAI_CHECK(resp.text.front() == '{' && resp.text.back() == '}');
}

ULTRAAI_TEST(ChatEmptyMessagesIsInvalid) {
    auto llm = CreateTextLLM(TextLLMConfig{});
    ChatResponse resp = llm->Chat(ChatRequest{});
    ULTRAAI_CHECK(resp.error.code == ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------- Embeddings

ULTRAAI_TEST(EmbeddingsBatchedAndDeterministic) {
    auto emb = CreateEmbeddings(EmbeddingsConfig{});
    EmbeddingsRequest req;
    req.texts = { "kettle", "teapot", "kettle" };
    req.dimensions = 16;

    EmbeddingsResponse resp = emb->Embed(req);
    ULTRAAI_CHECK(!resp.error);
    ULTRAAI_CHECK(resp.vectors.size() == 3);
    ULTRAAI_CHECK(resp.vectors[0].size() == 16);
    ULTRAAI_CHECK(resp.vectors[0] == resp.vectors[2]);  // same text -> same vector
    ULTRAAI_CHECK(resp.vectors[0] != resp.vectors[1]);  // different text differs
    ULTRAAI_CHECK(resp.usage.totalTokens > 0);
}

ULTRAAI_TEST(CosineSimilarityHelper) {
    std::vector<float> a = { 1, 0, 0 }, b = { 1, 0, 0 }, c = { 0, 1, 0 };
    ULTRAAI_CHECK(std::fabs(CosineSimilarity(a, b) - 1.0f) < 1e-6f);
    ULTRAAI_CHECK(std::fabs(CosineSimilarity(a, c)) < 1e-6f);
    ULTRAAI_CHECK(CosineSimilarity(a, { 1, 0 }) == 0.0f);      // size mismatch
    ULTRAAI_CHECK(CosineSimilarity({ 0, 0 }, { 0, 0 }) == 0.0f); // zero magnitude
}

// ---------------------------------------------------------------- Translator

ULTRAAI_TEST(TranslateBatchWithGlossaryAndAutoDetect) {
    auto tr = CreateTranslator(TranslatorConfig{});
    TranslateRequest req;
    req.texts = { "hello world", "goodbye world" };
    req.targetLanguage = "de";
    req.glossary["world"] = "Welt";

    TranslateResult res = tr->Translate(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.translations.size() == 2);
    ULTRAAI_CHECK(res.translations[0].text == "[de] hello Welt");
    ULTRAAI_CHECK(res.translations[0].detectedSourceLanguage == "en");
}

ULTRAAI_TEST(TranslateRequiresTarget) {
    auto tr = CreateTranslator(TranslatorConfig{});
    TranslateRequest req; req.texts = { "x" };
    ULTRAAI_CHECK(tr->Translate(req).error.code == ErrorCode::InvalidArgument);
}

ULTRAAI_TEST(DetectLanguage) {
    auto tr = CreateTranslator(TranslatorConfig{});
    LanguageDetection d = tr->DetectLanguage("hello");
    ULTRAAI_CHECK(!d.error);
    ULTRAAI_CHECK(d.language == "en");
    ULTRAAI_CHECK(d.confidence > 0.5);
    ULTRAAI_CHECK(tr->DetectLanguage("").error.code == ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------- CodeAssist

ULTRAAI_TEST(CodeAssistAllTasks) {
    auto ca = CreateCodeAssist(CodeAssistConfig{});

    CodeRequest gen;
    gen.task = CodeTask::Generate; gen.language = "cpp";
    gen.instruction = "hello world";
    ULTRAAI_CHECK(!ca->Run(gen).output.empty());

    CodeRequest bugs;
    bugs.task = CodeTask::DetectBugs; bugs.code = "int x;";
    CodeResult br = ca->Run(bugs);
    ULTRAAI_CHECK(!br.error);
    ULTRAAI_CHECK(br.findings.size() == 1);
    ULTRAAI_CHECK(br.findings[0].line == 1);

    CodeRequest fim;
    fim.task = CodeTask::FillInMiddle;
    fim.prefix = "int main() {"; fim.suffix = "}";
    CodeResult fr = ca->Run(fim);
    ULTRAAI_CHECK(fr.output.find("int main() {") == 0);
    ULTRAAI_CHECK(fr.output.find("}") != std::string::npos);

    CodeRequest port;
    port.task = CodeTask::TranslateLanguage;
    port.language = "cpp"; port.code = "x";
    ULTRAAI_CHECK(ca->Run(port).error.code == ErrorCode::InvalidArgument); // no target
    port.targetLanguage = "rust";
    ULTRAAI_CHECK(!ca->Run(port).error);
}

int main() { std::printf("TestTextCapabilities: all assertions passed\n"); return 0; }
