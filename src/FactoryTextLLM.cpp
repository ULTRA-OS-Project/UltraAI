#include "UltraAITextLLM.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using TextLLMRegistry = detail::Registry<ITextLLM, TextLLMConfig>;

std::unique_ptr<ITextLLM> CreateTextLLM(const TextLLMConfig& config, Error* error) {
    return TextLLMRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListTextLLMProviders() {
    return TextLLMRegistry::Instance().List();
}

bool RegisterTextLLMProvider(const std::string& name, TextLLMFactory factory, bool makeDefault) {
    return TextLLMRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
