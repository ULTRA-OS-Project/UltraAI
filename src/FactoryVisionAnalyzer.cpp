#include "UltraAIVisionAnalyzer.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using VisionAnalyzerRegistry = detail::Registry<IVisionAnalyzer, VisionAnalyzerConfig>;

std::unique_ptr<IVisionAnalyzer> CreateVisionAnalyzer(const VisionAnalyzerConfig& config, Error* error) {
    return VisionAnalyzerRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListVisionAnalyzerProviders() {
    return VisionAnalyzerRegistry::Instance().List();
}

bool RegisterVisionAnalyzerProvider(const std::string& name, VisionAnalyzerFactory factory, bool makeDefault) {
    return VisionAnalyzerRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
