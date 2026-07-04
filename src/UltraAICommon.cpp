#include "UltraAICommon.h"
#include "UltraAIEmbeddings.h"

#include <cmath>

namespace UltraAI {

const char* ErrorCodeName(ErrorCode code) {
    switch (code) {
        case ErrorCode::None:                return "None";
        case ErrorCode::InvalidArgument:     return "InvalidArgument";
        case ErrorCode::NotSupported:        return "NotSupported";
        case ErrorCode::ProviderUnavailable: return "ProviderUnavailable";
        case ErrorCode::AuthFailure:         return "AuthFailure";
        case ErrorCode::Network:             return "Network";
        case ErrorCode::RateLimited:         return "RateLimited";
        case ErrorCode::Timeout:             return "Timeout";
        case ErrorCode::Cancelled:           return "Cancelled";
        case ErrorCode::ContentFiltered:     return "ContentFiltered";
        case ErrorCode::Internal:            return "Internal";
    }
    return "Unknown";
}

float CosineSimilarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size() || a.empty()) return 0.0f;
    double dot = 0.0, magA = 0.0, magB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot  += static_cast<double>(a[i]) * b[i];
        magA += static_cast<double>(a[i]) * a[i];
        magB += static_cast<double>(b[i]) * b[i];
    }
    if (magA == 0.0 || magB == 0.0) return 0.0f;
    return static_cast<float>(dot / (std::sqrt(magA) * std::sqrt(magB)));
}

} // namespace UltraAI
