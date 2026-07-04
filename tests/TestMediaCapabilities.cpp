// TestMediaCapabilities.cpp — ISpeechToText, ITextToSpeech, IImageGen,
// IVisionAnalyzer, IVideoGen, IMusicGen.
#include "TestHelpers.h"
#include <UltraAI.h>

using namespace UltraAI;

static MediaBlob FakeAudio() { return MediaBlob::FromText("PCMPCMPCMPCM", "audio/wav"); }
static MediaBlob FakeImage() { return MediaBlob::FromText("PNGDATA", "image/png"); }
static MediaBlob FakeVideo() { return MediaBlob::FromText("MP4DATA", "video/mp4"); }

// ---------------------------------------------------------------- SpeechToText

ULTRAAI_TEST(TranscribeFileWithSegmentsAndWords) {
    SpeechToTextConfig cfg;
    cfg.options["mock.transcript"] = "hello ultra os";
    auto stt = CreateSpeechToText(cfg);

    TranscriptionRequest req;
    req.audio = FakeAudio();
    req.enableDiarization = true;
    req.enableWordTimestamps = true;

    TranscriptionResult res = stt->Transcribe(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.text == "hello ultra os");
    ULTRAAI_CHECK(res.segments.size() == 1);
    ULTRAAI_CHECK(res.segments[0].speakerId == 0);        // diarization on
    ULTRAAI_CHECK(res.segments[0].words.size() == 3);     // word timestamps on
    ULTRAAI_CHECK(res.segments[0].words[1].startSec >
                  res.segments[0].words[0].startSec);
}

ULTRAAI_TEST(TranscribeEmptyAudioIsInvalid) {
    auto stt = CreateSpeechToText(SpeechToTextConfig{});
    TranscriptionResult res = stt->Transcribe(TranscriptionRequest{});
    ULTRAAI_CHECK(res.error.code == ErrorCode::InvalidArgument);
}

ULTRAAI_TEST(LiveMicSessionStreamsPartials) {
    SpeechToTextConfig cfg;
    cfg.options["mock.transcript"] = "live mic words";
    auto stt = CreateSpeechToText(cfg);

    int partials = 0; bool finalSeen = false;
    Error err;
    auto session = stt->StartStream(TranscriptionRequest{}, [&](const SttStreamEvent& ev) {
        if (ev.kind == SttStreamEventKind::PartialText) ++partials;
        if (ev.kind == SttStreamEventKind::FinalSegment) finalSeen = true;
    }, &err);
    ULTRAAI_CHECK(session && !err);

    std::uint8_t chunk[16] = {};
    ULTRAAI_CHECK(!session->PushAudio(chunk, sizeof(chunk)));
    ULTRAAI_CHECK(!session->PushAudio(chunk, sizeof(chunk)));

    TranscriptionResult res = session->Finish();
    ULTRAAI_CHECK(res.text == "live mic words");
    ULTRAAI_CHECK(partials == 2);
    ULTRAAI_CHECK(finalSeen);
}

// ---------------------------------------------------------------- TextToSpeech

ULTRAAI_TEST(SpeakAndListVoices) {
    auto tts = CreateTextToSpeech(TextToSpeechConfig{});
    Error err;
    auto voices = tts->ListVoices(&err);
    ULTRAAI_CHECK(!err);
    ULTRAAI_CHECK(voices.size() >= 2);

    SpeakRequest req;
    req.text = "welcome to ultra os";
    req.voiceId = voices[0].id;
    SpeakResult res = tts->Speak(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(!res.audio.empty());
    ULTRAAI_CHECK(res.audio.mimeType == "audio/wav");

    ULTRAAI_CHECK(tts->Speak(SpeakRequest{}).error.code == ErrorCode::InvalidArgument);
}

ULTRAAI_TEST(SpeakStreamChunksReassemble) {
    auto tts = CreateTextToSpeech(TextToSpeechConfig{});
    SpeakRequest req; req.text = "chunked playback test";

    SpeakResult whole = tts->Speak(req);
    std::vector<std::uint8_t> assembled;
    int chunks = 0; bool sawLast = false;
    Error err = tts->SpeakStream(req, [&](const MediaBlob& chunk, bool last) {
        assembled.insert(assembled.end(), chunk.data.begin(), chunk.data.end());
        ++chunks; if (last) sawLast = true;
    });
    ULTRAAI_CHECK(!err);
    ULTRAAI_CHECK(chunks >= 2);
    ULTRAAI_CHECK(sawLast);
    ULTRAAI_CHECK(assembled == whole.audio.data);
}

ULTRAAI_TEST(VoiceCloningAppearsInVoiceList) {
    auto tts = CreateTextToSpeech(TextToSpeechConfig{});
    Error err;
    Voice v = tts->CloneVoice("Stefan", { FakeAudio() }, &err);
    ULTRAAI_CHECK(!err);
    ULTRAAI_CHECK(v.cloned);
    ULTRAAI_CHECK(v.name == "Stefan");

    bool found = false;
    for (const auto& voice : tts->ListVoices())
        if (voice.id == v.id) found = true;
    ULTRAAI_CHECK(found);

    tts->CloneVoice("NoSamples", {}, &err);
    ULTRAAI_CHECK(err.code == ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------- ImageGen

ULTRAAI_TEST(TextToImageWithProgressAndSeed) {
    auto ig = CreateImageGen(ImageGenConfig{});
    ImageGenRequest req;
    req.prompt = "a kettle on a stove";
    req.count = 2;
    req.seed = 42u;

    int progressCalls = 0; double lastFraction = 0.0;
    ImageGenResult res = ig->Generate(req, [&](double f, const std::string&) {
        ++progressCalls; lastFraction = f;
    });
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(res.images.size() == 2);
    ULTRAAI_CHECK(res.seedUsed == 42u);
    ULTRAAI_CHECK(progressCalls == 4);
    ULTRAAI_CHECK(lastFraction == 1.0);
}

ULTRAAI_TEST(ImageEditModesValidateInputs) {
    auto ig = CreateImageGen(ImageGenConfig{});

    ImageGenRequest img2img;
    img2img.mode = ImageGenMode::ImageToImage;
    ULTRAAI_CHECK(ig->Generate(img2img).error.code == ErrorCode::InvalidArgument);
    img2img.initImage = FakeImage();
    ULTRAAI_CHECK(!ig->Generate(img2img).error);

    ImageGenRequest inpaint;
    inpaint.mode = ImageGenMode::Inpaint;
    inpaint.initImage = FakeImage();
    ULTRAAI_CHECK(ig->Generate(inpaint).error.code == ErrorCode::InvalidArgument);
    inpaint.maskImage = FakeImage();
    ULTRAAI_CHECK(!ig->Generate(inpaint).error);

    ImageGenRequest cn;
    cn.controlNets.push_back({ "canny", FakeImage(), 0.7 });
    ULTRAAI_CHECK(!ig->Generate(cn).error);
}

// ---------------------------------------------------------------- VisionAnalyzer

ULTRAAI_TEST(AnalyzeMultiTask) {
    auto va = CreateVisionAnalyzer(VisionAnalyzerConfig{});
    AnalyzeRequest req;
    req.image = FakeImage();
    req.tasks = { VisionTask::Caption, VisionTask::Tags, VisionTask::Detection,
                  VisionTask::OCR, VisionTask::Faces, VisionTask::Safety,
                  VisionTask::VQA };
    req.question = "what is this?";

    AnalyzeResult res = va->Analyze(req);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(!res.caption.empty());
    ULTRAAI_CHECK(res.tags.size() == 2);
    ULTRAAI_CHECK(res.detections.size() == 1);
    ULTRAAI_CHECK(res.detections[0].box.w > 0);
    ULTRAAI_CHECK(!res.ocrText.empty());
    ULTRAAI_CHECK(res.ocrBlocks[0].blockType == "paragraph");
    ULTRAAI_CHECK(res.faces.size() == 1);
    ULTRAAI_CHECK(!res.safetyScores.empty());
    ULTRAAI_CHECK(res.answer.find("what is this?") != std::string::npos);

    ULTRAAI_CHECK(va->Analyze(AnalyzeRequest{}).error.code ==
                  ErrorCode::InvalidArgument);
}

// ---------------------------------------------------------------- VideoGen

ULTRAAI_TEST(VideoModesAndAudioTrack) {
    auto vg = CreateVideoGen(VideoGenConfig{});

    VideoGenRequest t2v;
    t2v.prompt = "a rotating kettle";
    t2v.withAudio = true; t2v.audioPrompt = "soft hum";
    int progress = 0;
    VideoGenResult res = vg->Generate(t2v, [&](double, const std::string&) { ++progress; });
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(!res.video.empty());
    ULTRAAI_CHECK(!res.audio.empty());
    ULTRAAI_CHECK(progress == 4);

    VideoGenRequest i2v; i2v.mode = VideoGenMode::ImageToVideo;
    ULTRAAI_CHECK(vg->Generate(i2v).error.code == ErrorCode::InvalidArgument);
    i2v.initImage = FakeImage();
    ULTRAAI_CHECK(!vg->Generate(i2v).error);

    VideoGenRequest up; up.mode = VideoGenMode::Upscale;
    ULTRAAI_CHECK(vg->Generate(up).error.code == ErrorCode::InvalidArgument);
    up.initVideo = FakeVideo();
    ULTRAAI_CHECK(!vg->Generate(up).error);
}

// ---------------------------------------------------------------- MusicGen

ULTRAAI_TEST(MusicSongStemsAndContinuation) {
    auto mg = CreateMusicGen(MusicGenConfig{});

    MusicGenRequest song;
    song.mode = MusicGenMode::Song;
    song.prompt = "upbeat synthwave";
    song.wantStems = true;
    MusicGenResult res = mg->Generate(song);
    ULTRAAI_CHECK(!res.error);
    ULTRAAI_CHECK(!res.audio.empty());
    ULTRAAI_CHECK(!res.vocals.empty());          // Song mode separates vocals
    ULTRAAI_CHECK(!res.lyricsUsed.empty());      // lyrics generated when absent
    ULTRAAI_CHECK(res.stems.count("drums") == 1);

    MusicGenRequest given = song;
    given.lyrics = "custom words";
    ULTRAAI_CHECK(mg->Generate(given).lyricsUsed == "custom words");

    MusicGenRequest cont; cont.mode = MusicGenMode::Continuation;
    ULTRAAI_CHECK(mg->Generate(cont).error.code == ErrorCode::InvalidArgument);
    cont.continuationAudio = FakeAudio();
    ULTRAAI_CHECK(!mg->Generate(cont).error);
}

int main() { std::printf("TestMediaCapabilities: all assertions passed\n"); return 0; }
