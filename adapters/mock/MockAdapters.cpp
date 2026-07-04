// MockAdapters.cpp — in-process mock adapters for every UltraAI capability.
//
// Every mock is deterministic and programmable through ProviderConfig.options:
//
//   mock.reply          fixed reply text (TextLLM, CodeAssist)
//   mock.transcript     fixed transcript (SpeechToText)
//   mock.fail           non-empty -> every call fails with Internal + this message
//   mock.dimensions     embedding vector size (default 8)
//   mock.progress_steps number of progress callbacks for gen jobs (default 4)
//
// No network, no external models — apps and tests run fully offline.

#include "UltraAI.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace UltraAI {
namespace mock {

// ---------------------------------------------------------------- helpers

static std::string Opt(const OptionsMap& o, const std::string& key,
                       const std::string& fallback = "") {
    auto it = o.find(key);
    return it == o.end() ? fallback : it->second;
}

static int OptInt(const OptionsMap& o, const std::string& key, int fallback) {
    auto it = o.find(key);
    if (it == o.end()) return fallback;
    try { return std::stoi(it->second); } catch (...) { return fallback; }
}

static Error FailIfConfigured(const OptionsMap& o) {
    const std::string msg = Opt(o, "mock.fail");
    if (!msg.empty()) return Error::Make(ErrorCode::Internal, msg);
    return {};
}

// FNV-1a — stable across platforms, used to derive deterministic vectors.
static std::uint64_t Fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

// Inherits the capability interface directly and supplies the IProvider
// boilerplate; RawProvider() returns the mock itself as the escape hatch.
template <typename ConfigT, typename InterfaceT>
class MockBase : public InterfaceT {
public:
    explicit MockBase(ConfigT cfg) : cfg_(std::move(cfg)) {}
    std::string ProviderId() const override { return "mock"; }
    void* RawProvider() override { return this; }
protected:
    ConfigT cfg_;
};

// ---------------------------------------------------------------- TextLLM

class MockTextLLM final : public MockBase<TextLLMConfig, ITextLLM> {
public:
    using MockBase::MockBase;

    ChatResponse Chat(const ChatRequest& req) override {
        ChatResponse resp;
        if ((resp.error = FailIfConfigured(cfg_.options))) return resp;
        if (req.messages.empty()) {
            resp.error = Error::Make(ErrorCode::InvalidArgument, "messages is empty");
            return resp;
        }

        // Tool round-trip: if tools are offered and no tool result is present
        // yet, request the first tool once.
        const bool haveToolResult = std::any_of(
            req.messages.begin(), req.messages.end(),
            [](const Message& m) { return m.role == Role::Tool; });
        if (!req.tools.empty() && !haveToolResult) {
            ToolCall call;
            call.id            = "mock-call-1";
            call.name          = req.tools.front().name;
            call.argumentsJson = "{}";
            resp.toolCalls.push_back(std::move(call));
            resp.finishReason = FinishReason::ToolUse;
            return resp;
        }

        const std::string& lastUser = req.messages.back().text;
        if (!req.responseJsonSchema.empty()) {
            resp.text = "{\"mock\":true,\"echo\":\"" + lastUser + "\"}";
        } else {
            resp.text = Opt(cfg_.options, "mock.reply", "[mock] " + lastUser);
        }
        resp.finishReason       = FinishReason::Stop;
        resp.usage.inputTokens  = lastUser.size() / 4 + 1;
        resp.usage.outputTokens = resp.text.size() / 4 + 1;
        resp.usage.totalTokens  = resp.usage.inputTokens + resp.usage.outputTokens;
        return resp;
    }

    Error ChatStream(const ChatRequest& req, StreamCallback onEvent) override {
        if (!onEvent) return Error::Make(ErrorCode::InvalidArgument, "null callback");
        ChatResponse full = Chat(req);
        if (full.error) {
            StreamEvent ev; ev.kind = StreamEventKind::Error; ev.error = full.error;
            onEvent(ev);
            return full.error;
        }
        // Emit the reply word-by-word, then Done with usage.
        std::istringstream words(full.text);
        std::string word;
        bool first = true;
        while (words >> word) {
            StreamEvent ev;
            ev.kind      = StreamEventKind::TextDelta;
            ev.textDelta = (first ? "" : " ") + word;
            first = false;
            onEvent(ev);
        }
        StreamEvent done;
        done.kind  = StreamEventKind::Done;
        done.usage = full.usage;
        onEvent(done);
        return {};
    }

    std::uint64_t CountTokens(const ChatRequest& req) override {
        std::uint64_t n = 0;
        for (const auto& m : req.messages) n += m.text.size() / 4 + 1;
        return n;
    }
};

// ---------------------------------------------------------------- Embeddings

class MockEmbeddings final : public MockBase<EmbeddingsConfig, IEmbeddings> {
public:
    using MockBase::MockBase;

    EmbeddingsResponse Embed(const EmbeddingsRequest& req) override {
        EmbeddingsResponse resp;
        if ((resp.error = FailIfConfigured(cfg_.options))) return resp;

        const int dims = req.dimensions > 0
            ? req.dimensions
            : OptInt(cfg_.options, "mock.dimensions", 8);

        for (const auto& text : req.texts) {
            std::vector<float> v(static_cast<std::size_t>(dims));
            std::uint64_t h = Fnv1a(text);
            for (auto& f : v) {
                h = h * 6364136223846793005ULL + 1442695040888963407ULL;
                f = static_cast<float>(static_cast<std::int64_t>(h >> 16) % 1000) / 1000.0f;
            }
            resp.vectors.push_back(std::move(v));
            resp.usage.inputTokens += text.size() / 4 + 1;
        }
        resp.usage.totalTokens = resp.usage.inputTokens;
        return resp;
    }
};

// ---------------------------------------------------------------- SpeechToText

class MockSttSession final : public ISttSession {
public:
    MockSttSession(std::string transcript, SttStreamCallback cb)
        : transcript_(std::move(transcript)), cb_(std::move(cb)) {}

    Error PushAudio(const std::uint8_t* data, std::size_t size) override {
        if (!data && size) return Error::Make(ErrorCode::InvalidArgument, "null audio");
        bytes_ += size;
        if (cb_) {
            SttStreamEvent ev;
            ev.kind        = SttStreamEventKind::PartialText;
            ev.partialText = transcript_.substr(0, std::min(transcript_.size(), bytes_ / 4));
            cb_(ev);
        }
        return {};
    }

    TranscriptionResult Finish() override {
        TranscriptionResult res;
        res.text             = transcript_;
        res.detectedLanguage = "en";
        TranscriptSegment seg;
        seg.text = transcript_; seg.startSec = 0.0; seg.endSec = 1.0;
        res.segments.push_back(seg);
        if (cb_) {
            SttStreamEvent ev;
            ev.kind    = SttStreamEventKind::FinalSegment;
            ev.segment = res.segments.front();
            cb_(ev);
        }
        return res;
    }

private:
    std::string       transcript_;
    SttStreamCallback cb_;
    std::size_t       bytes_ = 0;
};

class MockSpeechToText final : public MockBase<SpeechToTextConfig, ISpeechToText> {
public:
    using MockBase::MockBase;

    TranscriptionResult Transcribe(const TranscriptionRequest& req) override {
        TranscriptionResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;
        if (req.audio.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "audio is empty");
            return res;
        }
        res.text = Opt(cfg_.options, "mock.transcript", "mock transcript");
        res.detectedLanguage =
            req.languageHint.empty() ? "en" : req.languageHint;

        TranscriptSegment seg;
        seg.text = res.text; seg.startSec = 0.0; seg.endSec = 2.5;
        if (req.enableDiarization) seg.speakerId = 0;
        if (req.enableWordTimestamps) {
            std::istringstream words(res.text);
            std::string w; double t = 0.0;
            while (words >> w) {
                seg.words.push_back({ w, t, t + 0.4 });
                t += 0.5;
            }
        }
        res.segments.push_back(std::move(seg));
        return res;
    }

    std::unique_ptr<ISttSession> StartStream(const TranscriptionRequest& /*req*/,
                                             SttStreamCallback onEvent,
                                             Error* error) override {
        if (error) *error = Error::None_();
        return std::unique_ptr<ISttSession>(new MockSttSession(
            Opt(cfg_.options, "mock.transcript", "mock live transcript"),
            std::move(onEvent)));
    }
};

// ---------------------------------------------------------------- TextToSpeech

class MockTextToSpeech final : public MockBase<TextToSpeechConfig, ITextToSpeech> {
public:
    using MockBase::MockBase;

    std::vector<Voice> ListVoices(Error* error) override {
        if (error) *error = Error::None_();
        std::vector<Voice> voices = {
            { "mock-alto",  "Mock Alto",  "en-US", "female",  "default mock voice", false },
            { "mock-basso", "Mock Basso", "de-DE", "male",    "low mock voice",     false },
        };
        voices.insert(voices.end(), cloned_.begin(), cloned_.end());
        return voices;
    }

    SpeakResult Speak(const SpeakRequest& req) override {
        SpeakResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;
        if (req.text.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "text is empty");
            return res;
        }
        // "Audio" payload = the text bytes tagged with the requested format,
        // so tests can verify round-trips without a codec.
        res.audio = MediaBlob::FromText("MOCKAUDIO:" + req.voiceId + ":" + req.text,
                                        req.audioFormat);
        return res;
    }

    Error SpeakStream(const SpeakRequest& req, TtsChunkCallback onChunk) override {
        if (!onChunk) return Error::Make(ErrorCode::InvalidArgument, "null callback");
        SpeakResult full = Speak(req);
        if (full.error) return full.error;
        const auto& bytes = full.audio.data;
        const std::size_t chunkSize = std::max<std::size_t>(1, bytes.size() / 3);
        for (std::size_t off = 0; off < bytes.size(); off += chunkSize) {
            MediaBlob chunk;
            chunk.mimeType = full.audio.mimeType;
            const std::size_t n = std::min(chunkSize, bytes.size() - off);
            chunk.data.assign(bytes.begin() + off, bytes.begin() + off + n);
            onChunk(chunk, off + n >= bytes.size());
        }
        return {};
    }

    Voice CloneVoice(const std::string& name,
                     const std::vector<MediaBlob>& samples,
                     Error* error) override {
        if (samples.empty()) {
            if (error) *error = Error::Make(ErrorCode::InvalidArgument, "no samples");
            return {};
        }
        Voice v;
        v.id = "mock-clone-" + std::to_string(cloned_.size() + 1);
        v.name = name; v.language = "en-US"; v.cloned = true;
        cloned_.push_back(v);
        if (error) *error = Error::None_();
        return v;
    }

private:
    std::vector<Voice> cloned_;
};

// ---------------------------------------------------------------- shared gen helper

static void RunProgress(const OptionsMap& opts, const JobProgressCallback& cb) {
    if (!cb) return;
    const int steps = OptInt(opts, "mock.progress_steps", 4);
    for (int i = 1; i <= steps; ++i)
        cb(static_cast<double>(i) / steps, i == steps ? "done" : "rendering");
}

// ---------------------------------------------------------------- ImageGen

class MockImageGen final : public MockBase<ImageGenConfig, IImageGen> {
public:
    using MockBase::MockBase;

    ImageGenResult Generate(const ImageGenRequest& req,
                            JobProgressCallback onProgress) override {
        ImageGenResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;
        if (req.mode != ImageGenMode::TextToImage && req.initImage.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument,
                                    "initImage required for this mode");
            return res;
        }
        if (req.mode == ImageGenMode::Inpaint && req.maskImage.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument,
                                    "maskImage required for inpaint");
            return res;
        }
        RunProgress(cfg_.options, onProgress);
        res.seedUsed = req.seed ? *req.seed : Fnv1a(req.prompt);
        for (int i = 0; i < std::max(1, req.count); ++i) {
            res.images.push_back(MediaBlob::FromText(
                "MOCKIMAGE:" + std::to_string(req.width) + "x" +
                std::to_string(req.height) + ":" + req.prompt,
                "image/png"));
        }
        return res;
    }
};

// ---------------------------------------------------------------- VisionAnalyzer

class MockVisionAnalyzer final : public MockBase<VisionAnalyzerConfig, IVisionAnalyzer> {
public:
    using MockBase::MockBase;

    AnalyzeResult Analyze(const AnalyzeRequest& req) override {
        AnalyzeResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;
        if (req.image.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "image is empty");
            return res;
        }
        for (VisionTask task : req.tasks) {
            switch (task) {
                case VisionTask::Caption:
                    res.caption = "a mock image"; break;
                case VisionTask::Tags:
                    res.tags = { { "mock", 0.99 }, { "test", 0.87 } }; break;
                case VisionTask::Detection:
                    res.detections = { { "object", 0.9, { 0.1, 0.1, 0.5, 0.5 } } }; break;
                case VisionTask::Segmentation: {
                    SegmentMask m;
                    m.label = "object";
                    m.mask  = MediaBlob::FromText("MOCKMASK", "image/png");
                    m.box   = { 0.1, 0.1, 0.5, 0.5 };
                    res.segments.push_back(std::move(m));
                    break;
                }
                case VisionTask::OCR:
                case VisionTask::DocumentLayout: {
                    res.ocrText = "mock ocr text";
                    OcrBlock b;
                    b.text = res.ocrText; b.box = { 0, 0, 1, 0.1 };
                    b.blockType = "paragraph";
                    res.ocrBlocks.push_back(std::move(b));
                    break;
                }
                case VisionTask::Faces:
                    res.faces.push_back({ { 0.4, 0.2, 0.2, 0.3 },
                                          { { "emotion", "neutral" } } });
                    break;
                case VisionTask::Safety:
                    res.safetyScores = { { "adult", 0.01 }, { "violence", 0.01 } };
                    break;
                case VisionTask::VQA:
                    res.answer = "[mock answer] " + req.question; break;
            }
        }
        return res;
    }
};

// ---------------------------------------------------------------- Translator

class MockTranslator final : public MockBase<TranslatorConfig, ITranslator> {
public:
    using MockBase::MockBase;

    TranslateResult Translate(const TranslateRequest& req) override {
        TranslateResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;
        if (req.targetLanguage.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "targetLanguage required");
            return res;
        }
        for (const auto& text : req.texts) {
            std::string out = text;
            for (const auto& kv : req.glossary) {
                std::size_t pos = 0;
                while ((pos = out.find(kv.first, pos)) != std::string::npos) {
                    out.replace(pos, kv.first.size(), kv.second);
                    pos += kv.second.size();
                }
            }
            Translation t;
            t.text = "[" + req.targetLanguage + "] " + out;
            t.detectedSourceLanguage =
                req.sourceLanguage.empty() ? "en" : req.sourceLanguage;
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
        d.language = "en"; d.confidence = 0.95;
        return d;
    }
};

// ---------------------------------------------------------------- VideoGen

class MockVideoGen final : public MockBase<VideoGenConfig, IVideoGen> {
public:
    using MockBase::MockBase;

    VideoGenResult Generate(const VideoGenRequest& req,
                            JobProgressCallback onProgress) override {
        VideoGenResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;
        if (req.mode == VideoGenMode::ImageToVideo && req.initImage.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "initImage required");
            return res;
        }
        if ((req.mode == VideoGenMode::VideoToVideo ||
             req.mode == VideoGenMode::Interpolation ||
             req.mode == VideoGenMode::Upscale) && req.initVideo.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument, "initVideo required");
            return res;
        }
        RunProgress(cfg_.options, onProgress);
        res.video = MediaBlob::FromText(
            "MOCKVIDEO:" + std::to_string(req.fps) + "fps:" + req.prompt, "video/mp4");
        if (req.withAudio)
            res.audio = MediaBlob::FromText("MOCKTRACK:" + req.audioPrompt, "audio/aac");
        return res;
    }
};

// ---------------------------------------------------------------- MusicGen

class MockMusicGen final : public MockBase<MusicGenConfig, IMusicGen> {
public:
    using MockBase::MockBase;

    MusicGenResult Generate(const MusicGenRequest& req,
                            JobProgressCallback onProgress) override {
        MusicGenResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;
        if (req.mode == MusicGenMode::Continuation && req.continuationAudio.empty()) {
            res.error = Error::Make(ErrorCode::InvalidArgument,
                                    "continuationAudio required");
            return res;
        }
        RunProgress(cfg_.options, onProgress);
        res.audio = MediaBlob::FromText("MOCKMUSIC:" + req.prompt, "audio/wav");
        if (req.mode == MusicGenMode::Song) {
            res.lyricsUsed = req.lyrics.empty() ? "la la la (mock lyrics)" : req.lyrics;
            res.vocals = MediaBlob::FromText("MOCKVOCALS", "audio/wav");
        }
        if (req.wantStems) {
            res.stems["drums"] = MediaBlob::FromText("MOCKSTEM:drums", "audio/wav");
            res.stems["bass"]  = MediaBlob::FromText("MOCKSTEM:bass",  "audio/wav");
        }
        return res;
    }
};

// ---------------------------------------------------------------- CodeAssist

class MockCodeAssist final : public MockBase<CodeAssistConfig, ICodeAssist> {
public:
    using MockBase::MockBase;

    CodeResult Run(const CodeRequest& req) override {
        CodeResult res;
        if ((res.error = FailIfConfigured(cfg_.options))) return res;

        const std::string fixed = Opt(cfg_.options, "mock.reply");
        switch (req.task) {
            case CodeTask::Generate:
                res.output = fixed.empty()
                    ? "// [mock:" + req.language + "] " + req.instruction : fixed;
                break;
            case CodeTask::Explain:
                res.output = "This code (" + std::to_string(req.code.size()) +
                             " bytes) is a mock explanation."; break;
            case CodeTask::Refactor:
                res.output = "// refactored: " + req.instruction + "\n" + req.code; break;
            case CodeTask::TranslateLanguage:
                if (req.targetLanguage.empty()) {
                    res.error = Error::Make(ErrorCode::InvalidArgument,
                                            "targetLanguage required");
                    return res;
                }
                res.output = "// ported " + req.language + " -> " +
                             req.targetLanguage + "\n" + req.code;
                break;
            case CodeTask::DetectBugs:
                res.findings.push_back(
                    { 1, FindingSeverity::Warning, "mock finding", "mock fix" });
                res.output = "1 finding";
                break;
            case CodeTask::GenerateTests:
                res.output = "// mock tests for input\nvoid test_mock() {}"; break;
            case CodeTask::GenerateDocs:
                res.output = "/// Mock documentation.\n" + req.code; break;
            case CodeTask::FillInMiddle:
                res.output = req.prefix + "/* mock middle */" + req.suffix; break;
        }
        res.usage.outputTokens = res.output.size() / 4 + 1;
        res.usage.totalTokens  = res.usage.outputTokens;
        return res;
    }
};

} // namespace mock

// ---------------------------------------------------------------- registration

namespace detail {

// Self-registration entry point. Factories call this lazily so the mock
// adapter is available even when UltraAI is linked as a static library.
void EnsureBuiltinAdapters() {
    static const bool once = [] {
        using namespace mock;
        RegisterTextLLMProvider("mock",
            [](const TextLLMConfig& c) { return std::unique_ptr<ITextLLM>(new MockTextLLM(c)); });
        RegisterEmbeddingsProvider("mock",
            [](const EmbeddingsConfig& c) { return std::unique_ptr<IEmbeddings>(new MockEmbeddings(c)); });
        RegisterSpeechToTextProvider("mock",
            [](const SpeechToTextConfig& c) { return std::unique_ptr<ISpeechToText>(new MockSpeechToText(c)); });
        RegisterTextToSpeechProvider("mock",
            [](const TextToSpeechConfig& c) { return std::unique_ptr<ITextToSpeech>(new MockTextToSpeech(c)); });
        RegisterImageGenProvider("mock",
            [](const ImageGenConfig& c) { return std::unique_ptr<IImageGen>(new MockImageGen(c)); });
        RegisterVisionAnalyzerProvider("mock",
            [](const VisionAnalyzerConfig& c) { return std::unique_ptr<IVisionAnalyzer>(new MockVisionAnalyzer(c)); });
        RegisterTranslatorProvider("mock",
            [](const TranslatorConfig& c) { return std::unique_ptr<ITranslator>(new MockTranslator(c)); });
        RegisterVideoGenProvider("mock",
            [](const VideoGenConfig& c) { return std::unique_ptr<IVideoGen>(new MockVideoGen(c)); });
        RegisterMusicGenProvider("mock",
            [](const MusicGenConfig& c) { return std::unique_ptr<IMusicGen>(new MockMusicGen(c)); });
        RegisterCodeAssistProvider("mock",
            [](const CodeAssistConfig& c) { return std::unique_ptr<ICodeAssist>(new MockCodeAssist(c)); });
        return true;
    }();
    (void)once;
}

} // namespace detail
} // namespace UltraAI
