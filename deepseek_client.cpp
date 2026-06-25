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

        std::string body = requestBody.dump();

        LOG_INFO("DeepSeekClient::Chat: sending request to {} model={} question_len={}", chatCompletionsEndpoint(), model_, question.size());

        auto headers = std::unordered_map<std::string, std::string>{
            {"Authorization", "Bearer " + apiKey_},
            {"Content-Type", "application/json"},
            {"Accept", "application/json"}
        };

        auto [responseStr, responseCode] = WebClient::Post(chatCompletionsEndpoint(), body, headers);

        LOG_INFO("DeepSeekClient::Chat: response code={} body_len={}", responseCode, responseStr.size());

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
            LOG_ERROR("DeepSeekClient::Chat: {}", result.errorMessage);
            return result;
        }

        auto responseJson = nlohmann::json::parse(responseStr);

        // Extract the answer from the response
        if (responseJson.contains("choices") && responseJson["choices"].is_array() &&
            !responseJson["choices"].empty()) {
            const auto& choice = responseJson["choices"][0];
            if (choice.contains("message") && choice["message"].contains("content")) {
                result.answer = choice["message"]["content"].get<std::string>();
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
        } else if (!conversationId.empty()) {
            result.conversationId = conversationId;
        }

        LOG_INFO("DeepSeekClient::Chat: success, model={} tokens={} answer_len={}", result.modelUsed, result.tokensUsed, result.answer.size());
        LOG_INFO("DeepSeekClient::Chat: result: {}", result.answer);
    } catch (const std::exception& e) {
        result.errorMessage = fmt::format("Exception: {}", e.what());
        LOG_ERROR("DeepSeekClient::Chat: {}", result.errorMessage);
    } catch (...) {
        result.errorMessage = "Unknown exception occurred.";
        LOG_ERROR("DeepSeekClient::Chat: {}", result.errorMessage);
    }

    return result;
}
