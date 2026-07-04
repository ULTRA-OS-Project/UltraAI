// UltraAICodeAssist.h — code generation and analysis tasks.
#pragma once

#include "UltraAICommon.h"

#include <functional>
#include <memory>

namespace UltraAI {

enum class CodeTask {
    Generate,           // instruction -> code
    Explain,            // code -> prose
    Refactor,           // code + instruction -> code
    TranslateLanguage,  // code (language) -> code (targetLanguage)
    DetectBugs,         // code -> findings
    GenerateTests,      // code -> test code
    GenerateDocs,       // code -> documented code / doc text
    FillInMiddle        // prefix + suffix -> middle
};

enum class FindingSeverity { Info, Warning, Error };

struct CodeFinding {
    int             line = 0;      // 1-based, 0 -> whole file
    FindingSeverity severity = FindingSeverity::Warning;
    std::string     message;
    std::string     suggestion;    // optional fix
};

struct CodeRequest {
    CodeTask    task = CodeTask::Generate;
    std::string language;          // "cpp", "python", ...
    std::string targetLanguage;    // TranslateLanguage
    std::string instruction;       // Generate / Refactor guidance
    std::string code;              // input source
    std::string prefix;            // FillInMiddle
    std::string suffix;            // FillInMiddle
    OptionsMap  options;
};

struct CodeResult {
    std::string              output;    // generated code or prose
    std::vector<CodeFinding> findings;  // DetectBugs
    TokenUsage               usage;
    Error                    error;
};

struct CodeAssistConfig : ProviderConfig {};

class ICodeAssist : public IProvider {
public:
    virtual CodeResult Run(const CodeRequest& request) = 0;
};

using CodeAssistFactory = std::function<std::unique_ptr<ICodeAssist>(const CodeAssistConfig&)>;

std::unique_ptr<ICodeAssist> CreateCodeAssist(const CodeAssistConfig& config, Error* error = nullptr);
std::vector<std::string>     ListCodeAssistProviders();
bool RegisterCodeAssistProvider(const std::string& name, CodeAssistFactory factory,
                                bool makeDefault = false);

} // namespace UltraAI
