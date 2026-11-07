#include "deepseek_client.h"
#include "webclient.h"
#include <nlohmann/json.hpp>
#include <fmt/format.h>
#include "logger/logger.h"

DeepSeekClient::DeepSeekClient(const std::string& apiKey, const std::string& baseUrl, const std::string& model)
    : apiKey_(apiKey), baseUrl_(baseUrl), model_(model) {
}

DeepSeekResponse DeepSeekClient::Chat(const std::string& question, const std::string& context, const std::string& conversationId) {
    DeepSeekResponse result;

    if (apiKey_.empty()) {
        result.errorMessage = "DeepSeek API key is not configured. Set 'deepseek_api_key' in server keys.";
        LOG_ERROR("DeepSeekClient::Chat: {}", result.errorMessage);
        return result;
    }

    if (question.empty()) {
        result.errorMessage = "Question cannot be empty.";
        return result;
    }

    try {
        nlohmann::json requestBody;
        requestBody["model"] = model_;
        requestBody["stream"] = false;

        nlohmann::json messages = nlohmann::json::array();

        // Add system context if provided
        if (!context.empty()) {
            messages.push_back({{"role", "system"}, {"content", context}});
        }

        // Build user message
        std::string userContent = question;

        // Add conversation history context if this is a multi-turn conversation
        // We pass the conversation_id as part of the message so DeepSeek can track it
        if (!conversationId.empty()) {
            // For multi-turn, we include the conversation_id hint in the system context
            // DeepSeek doesn't have native conversation ID support like OpenAI's thread,
            // so we include prior context hints in the system message
            nlohmann::json systemMsg;
            systemMsg["role"] = "system";
            systemMsg["content"] = fmt::format("[Conversation ID: {}] This is a follow-up in an ongoing conversation.", conversationId);
            messages.push_back(systemMsg);
        }

        messages.push_back({
            {"role", "user"},
            {"content", userContent}
        });

        requestBody["messages"] = messages;

        result = PostChat(requestBody.dump(), conversationId);
    } catch (const std::exception& e) {
        result.errorMessage = fmt::format("Exception: {}", e.what());
        LOG_ERROR("DeepSeekClient::Chat: {}", result.errorMessage);
    } catch (...) {
        result.errorMessage = "Unknown exception occurred.";
        LOG_ERROR("DeepSeekClient::Chat: {}", result.errorMessage);
    }

    return result;
}

DeepSeekResponse DeepSeekClient::ChatMessages(const std::vector<DeepSeekMessage>& messages,
                                             const std::string& toolsJson) {
    DeepSeekResponse result;

    if (apiKey_.empty()) {
        result.errorMessage = "AI API key is not configured.";
        LOG_ERROR("DeepSeekClient::ChatMessages: {}", result.errorMessage);
        return result;
    }
    if (messages.empty()) {
        result.errorMessage = "The conversation is empty.";
        return result;
    }

    try {
        nlohmann::json requestBody;
        requestBody["model"] = model_;
        requestBody["stream"] = false;

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& m : messages) {
            nlohmann::json entry;
            entry["role"] = m.role;
            entry["content"] = m.content;
            if (!m.toolCallId.empty()) entry["tool_call_id"] = m.toolCallId;
            if (!m.toolCalls.empty()) {
                nlohmann::json calls = nlohmann::json::array();
                for (const auto& call : m.toolCalls) {
                    calls.push_back({{"id", call.id},
                                     {"type", "function"},
                                     {{"function", {{"name", call.name}, {"arguments", call.arguments}}}}});
                }
                entry["tool_calls"] = calls;
            }
            arr.push_back(std::move(entry));
        }
        requestBody["messages"] = arr;

        if (!toolsJson.empty()) {
            try {
                requestBody["tools"] = nlohmann::json::parse(toolsJson);
                requestBody["tool_choice"] = "auto";
            } catch (...) {
                LOG_WARN("DeepSeekClient::ChatMessages: ignoring malformed tools JSON");
            }
        }

        result = PostChat(requestBody.dump(), "");
        if (result.errorMessage.empty()) {
            LOG_INFO("DeepSeekClient::ChatMessages: success, model={} tokens={} answer_len={}",
                     result.modelUsed, result.tokensUsed, result.answer.size());
        }
    } catch (const std::exception& e) {
        result.errorMessage = fmt::format("Exception: {}", e.what());
        LOG_ERROR("DeepSeekClient::ChatMessages: {}", result.errorMessage);
    } catch (...) {
        result.errorMessage = "Unknown exception occurred.";
        LOG_ERROR("DeepSeekClient::ChatMessages: {}", result.errorMessage);
    }

    return result;
}

bool DeepSeekClient::ListModels(std::vector<std::string>& out, std::string& errorMessage) {
    out.clear();
    errorMessage.clear();

    if (apiKey_.empty()) {
        errorMessage = "AI API key is not configured.";
        return false;
    }

    try {
        auto headers = std::unordered_map<std::string, std::string>{
            {"Authorization", "Bearer " + apiKey_},
            {"Accept", "application/json"}
        };

        auto [body, code] = WebClient::Get(modelsEndpoint(), headers);
        if (code != 200) {
            errorMessage = fmt::format("HTTP {} from {}", code, modelsEndpoint());
            return false;
        }

        auto j = nlohmann::json::parse(body);
        if (!j.contains("data") || !j["data"].is_array()) {
            errorMessage = "Unexpected /models response.";
            return false;
        }
        for (const auto& m : j["data"]) {
            if (m.contains("id") && m["id"].is_string()) {
                out.push_back(m["id"].get<std::string>());
            }
        }
        return true;
    } catch (const std::exception& e) {
        errorMessage = e.what();
        return false;
    } catch (...) {
        errorMessage = "Unknown exception while listing models.";
        return false;
    }
}

// ── Shared request/response path ──────────────────────────────────

DeepSeekResponse DeepSeekClient::PostChat(const std::string& requestBody,
                                          const std::string& fallbackConversationId) {
    DeepSeekResponse result;
    try {
        LOG_INFO("DeepSeekClient: sending request to {} model={} body_len={}",
                 chatCompletionsEndpoint(), model_, requestBody.size());

        auto headers = std::unordered_map<std::string, std::string>{
            {"Authorization", "Bearer " + apiKey_},
            {"Content-Type", "application/json"},
            {"Accept", "application/json"}
        };

        auto [responseStr, responseCode] =
            WebClient::PostWithTimeout(chatCompletionsEndpoint(), requestBody, headers, timeoutSeconds_);

        LOG_INFO("DeepSeekClient: response code={} body_len={}", responseCode, responseStr.size());

        if (responseCode != 200) {
            // Try to extract error message from response
            try {
                auto errorJson = nlohmann::json::parse(responseStr);
                if (errorJson.contains("error") && errorJson["error"].contains("message")) {
                    result.errorMessage = errorJson["error"]["message"].get<std::string>();
                } else {
                    result.errorMessage = fmt::format("HTTP {}: {}", responseCode, responseStr);
                }
            } catch (...) {
                result.errorMessage = fmt::format("HTTP {}: {}", responseCode, responseStr);
            }
            LOG_ERROR("DeepSeekClient: {}", result.errorMessage);
            return result;
        }

        auto responseJson = nlohmann::json::parse(responseStr);

        // Extract the answer from the response
        if (responseJson.contains("choices") && responseJson["choices"].is_array() &&
            !responseJson["choices"].empty()) {
            const auto& choice = responseJson["choices"][0];

            if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
                result.finishReason = choice["finish_reason"].get<std::string>();
            }

            if (choice.contains("message")) {
                const auto& message = choice["message"];
                if (message.contains("content") && message["content"].is_string()) {
                    result.answer = message["content"].get<std::string>();
                }
                // Tool calls: the model is asking the caller to fetch data.
                if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
                    for (const auto& tc : message["tool_calls"]) {
                        DeepSeekToolCall call;
                        if (tc.contains("id") && tc["id"].is_string()) {
                            call.id = tc["id"].get<std::string>();
                        }
                        if (tc.contains("function") && tc["function"].is_object()) {
                            const auto& fn = tc["function"];
                            if (fn.contains("name") && fn["name"].is_string()) {
                                call.name = fn["name"].get<std::string>();
                            }
                            if (fn.contains("arguments")) {
                                call.arguments = fn["arguments"].is_string()
                                                     ? fn["arguments"].get<std::string>()
                                                     : fn["arguments"].dump();
                            }
                        }
                        result.toolCalls.push_back(std::move(call));
                    }
                }
            }
        }

        // Extract model used
        if (responseJson.contains("model")) {
            result.modelUsed = responseJson["model"].get<std::string>();
        }

        // Extract token usage
        if (responseJson.contains("usage")) {
            if (responseJson["usage"].contains("total_tokens")) {
                result.tokensUsed = responseJson["usage"]["total_tokens"].get<int32_t>();
            }
        }

        // Use the response ID as conversation_id for multi-turn continuity
        if (responseJson.contains("id")) {
            result.conversationId = responseJson["id"].get<std::string>();
        } else if (!fallbackConversationId.empty()) {
            result.conversationId = fallbackConversationId;
        }

        if (result.answer.empty() && result.toolCalls.empty()) {
            result.errorMessage = "The model returned an empty answer.";
        }

        LOG_INFO("DeepSeekClient: success, model={} tokens={} answer_len={}",
                 result.modelUsed, result.tokensUsed, result.answer.size());
    } catch (const std::exception& e) {
        result.errorMessage = fmt::format("Exception: {}", e.what());
        LOG_ERROR("DeepSeekClient: {}", result.errorMessage);
    } catch (...) {
        result.errorMessage = "Unknown exception occurred.";
        LOG_ERROR("DeepSeekClient: {}", result.errorMessage);
    }

    return result;
}
