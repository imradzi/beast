#pragma once
#include <string>
#include <cstdint>
#include <memory>

struct DeepSeekResponse {
    std::string answer;
    std::string modelUsed;
    int32_t tokensUsed = 0;
    std::string conversationId;
    std::string errorMessage;
};

class DeepSeekClient {
public:
    DeepSeekClient(const std::string& apiKey, const std::string& baseUrl = "https://api.deepseek.com", const std::string& model = "deepseek-chat");

    /// Send a question to the DeepSeek Chat Completions API.
    /// @param question     The user's question.
    /// @param context      Optional background/context to prepend.
    /// @param conversationId  Optional conversation ID for multi-turn. If empty, starts a new conversation.
    /// @return DeepSeekResponse with answer, model, tokens, conversation_id, or error.
    DeepSeekResponse Chat(const std::string& question, const std::string& context = "", const std::string& conversationId = "");

    /// Set a new API key at runtime.
    void SetApiKey(const std::string& apiKey) { apiKey_ = apiKey; }

    /// Set a new base URL at runtime (e.g. to switch from DeepSeek to OpenAI).
    void SetBaseUrl(const std::string& baseUrl) { baseUrl_ = baseUrl; }

    /// Set a new model at runtime.
    void SetModel(const std::string& model) { model_ = model; }

private:
    std::string apiKey_;
    std::string baseUrl_;
    std::string model_;
    std::string chatCompletionsEndpoint() const { return baseUrl_ + "/v1/chat/completions"; }
};
