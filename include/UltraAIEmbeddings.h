// UltraAIEmbeddings.h — batched text embeddings.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <memory>

namespace UltraAI {

struct EmbeddingsRequest {
    std::vector<std::string> texts;       // batch input
    int                      dimensions = 0; // 0 -> provider default
    OptionsMap               options;
};

struct EmbeddingsResponse {
    std::vector<std::vector<float>> vectors; // one per input text
    TokenUsage                      usage;
    Error                           error;
};

struct EmbeddingsConfig : ProviderConfig {};

class IEmbeddings : public IProvider {
public:
    virtual EmbeddingsResponse Embed(const EmbeddingsRequest& request) = 0;
};

// Convenience helper: cosine similarity between two vectors.
// Returns 0.0 for mismatched sizes or zero-magnitude input.
float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);

using EmbeddingsFactory = std::function<std::unique_ptr<IEmbeddings>(const EmbeddingsConfig&)>;

std::unique_ptr<IEmbeddings> CreateEmbeddings(const EmbeddingsConfig& config, Error* error = nullptr);
std::vector<std::string>     ListEmbeddingsProviders();
bool RegisterEmbeddingsProvider(const std::string& name, EmbeddingsFactory factory,
                                bool makeDefault = false);

} // namespace UltraAI
