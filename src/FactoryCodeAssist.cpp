#include "UltraAICodeAssist.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using CodeAssistRegistry = detail::Registry<ICodeAssist, CodeAssistConfig>;

std::unique_ptr<ICodeAssist> CreateCodeAssist(const CodeAssistConfig& config, Error* error) {
    return CodeAssistRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListCodeAssistProviders() {
    return CodeAssistRegistry::Instance().List();
}

bool RegisterCodeAssistProvider(const std::string& name, CodeAssistFactory factory, bool makeDefault) {
    return CodeAssistRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
