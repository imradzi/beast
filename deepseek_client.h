#pragma once
#include <string>
#include <cstdint>
#include <memory>
#include <vector>

/// A tool invocation requested by the model. `arguments` is the raw JSON string
/// exactly as returned by the provider — parse it, never assume it is valid.
struct DeepSeekToolCall {
    std::string id;
    std::string name;
    std::string arguments;
};

struct DeepSeekResponse {
    std::string answer;
    std::string modelUsed;
    int32_t tokensUsed = 0;
    std::string conversationId;
    std::string errorMessage;
    std::string finishReason;              // "stop", "tool_calls", ...
    std::vector<DeepSeekToolCall> toolCalls;  // non-empty when the model wants data
};

/// One assistant message. `role` uses the provider vocabulary:
/// "system", "user", "assistant" or "tool".
/// - assistant + toolCalls: the model is asking the caller to run tools.
/// - role == "tool": `toolCallId` names the call this message answers.
struct DeepSeekMessage {
    std::string role;
    std::string content;
    std::string toolCallId;
    std::vector<DeepSeekToolCall> toolCalls;

    DeepSeekMessage() = default;
    DeepSeekMessage(std::string role_, std::string content_)
        : role(std::move(role_)), content(std::move(content_)) {}
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

    /// Send a fully formed conversation. This is the real multi-turn path — the
    /// caller owns the system message, the history and the new question. Used by
    /// the gateway assistant, which keeps history per chat session.
    /// @param toolsJson  Optional JSON array of function schemas (OpenAI/DeepSeek
    ///                   `tools` format). Empty disables tool calling.
    DeepSeekResponse ChatMessages(const std::vector<DeepSeekMessage>& messages,
                                  const std::string& toolsJson = "");

    /// GET {baseUrl}/v1/models — the model ids this key is allowed to use.
    bool ListModels(std::vector<std::string>& out, std::string& errorMessage);

    /// Set a new API key at runtime.
    void SetApiKey(const std::string& apiKey) { apiKey_ = apiKey; }

    /// Set a new base URL at runtime (e.g. to switch from DeepSeek to OpenAI).
    void SetBaseUrl(const std::string& baseUrl) { baseUrl_ = baseUrl; }

    /// Set a new model at runtime.
    void SetModel(const std::string& model) { model_ = model; }

    /// Total request timeout in seconds (default 30, matching WebClient::Post).
    void SetTimeout(int seconds) { timeoutSeconds_ = seconds; }

    bool HasApiKey() const { return !apiKey_.empty(); }
    const std::string& Model() const { return model_; }
    const std::string& BaseUrl() const { return baseUrl_; }

private:
    /// Shared request/parse path for the two public Chat* entry points.
    DeepSeekResponse PostChat(const std::string& requestBody, const std::string& fallbackConversationId);

    std::string apiKey_;
    std::string baseUrl_;
    std::string model_;
    int timeoutSeconds_ = 30;
    std::string chatCompletionsEndpoint() const { return baseUrl_ + "/v1/chat/completions"; }
    std::string modelsEndpoint() const { return baseUrl_ + "/v1/models"; }
};
