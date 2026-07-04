#include "UltraAISpeechToText.h"
#include "UltraAIRegistry.h"

namespace UltraAI {

using SpeechToTextRegistry = detail::Registry<ISpeechToText, SpeechToTextConfig>;

std::unique_ptr<ISpeechToText> CreateSpeechToText(const SpeechToTextConfig& config, Error* error) {
    return SpeechToTextRegistry::Instance().Create(config, error);
}

std::vector<std::string> ListSpeechToTextProviders() {
    return SpeechToTextRegistry::Instance().List();
}

bool RegisterSpeechToTextProvider(const std::string& name, SpeechToTextFactory factory, bool makeDefault) {
    return SpeechToTextRegistry::Instance().Register(name, std::move(factory), makeDefault);
}

} // namespace UltraAI
