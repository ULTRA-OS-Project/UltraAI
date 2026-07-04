// UltraAIVisionAnalyzer.h — image understanding.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <memory>

namespace UltraAI {

enum class VisionTask {
    Caption, Tags, Detection, Segmentation, OCR, DocumentLayout, Faces, Safety, VQA
};

struct BoundingBox { double x = 0, y = 0, w = 0, h = 0; }; // normalized [0,1]

struct LabelScore {
    std::string label;
    double      confidence = 0.0;
};

struct Detection {
    std::string label;
    double      confidence = 0.0;
    BoundingBox box;
};

struct SegmentMask {
    std::string label;
    MediaBlob   mask;         // grayscale mask image
    BoundingBox box;
};

struct OcrBlock {
    std::string text;
    BoundingBox box;
    std::string blockType;    // "paragraph", "heading", "table", "figure", ...
};

struct FaceInfo {
    BoundingBox box;
    OptionsMap  attributes;   // "age"->"31", "emotion"->"neutral", ...
};

struct AnalyzeRequest {
    MediaBlob               image;
    std::vector<VisionTask> tasks;
    std::string             question;  // VQA
    OptionsMap              options;
};

struct AnalyzeResult {
    std::string              caption;
    std::vector<LabelScore>  tags;
    std::vector<Detection>   detections;
    std::vector<SegmentMask> segments;
    std::string              ocrText;
    std::vector<OcrBlock>    ocrBlocks;     // includes document layout
    std::vector<FaceInfo>    faces;
    std::vector<LabelScore>  safetyScores;  // category -> confidence
    std::string              answer;        // VQA
    Error                    error;
};

struct VisionAnalyzerConfig : ProviderConfig {};

class IVisionAnalyzer : public IProvider {
public:
    virtual AnalyzeResult Analyze(const AnalyzeRequest& request) = 0;
};

using VisionAnalyzerFactory = std::function<std::unique_ptr<IVisionAnalyzer>(const VisionAnalyzerConfig&)>;

std::unique_ptr<IVisionAnalyzer> CreateVisionAnalyzer(const VisionAnalyzerConfig& config, Error* error = nullptr);
std::vector<std::string>         ListVisionAnalyzerProviders();
bool RegisterVisionAnalyzerProvider(const std::string& name, VisionAnalyzerFactory factory,
                                    bool makeDefault = false);

} // namespace UltraAI
