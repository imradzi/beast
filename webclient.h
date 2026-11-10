#include <curl/curl.h>
#include <string>
#include <tuple>
#include <unordered_map>
class WebClient {
public:
    static std::tuple<std::string, int64_t> Post(const std::string &url, const std::string &body, const std::unordered_map<std::string, std::string> &headers = {});
    /// Same as Post but with explicit connect/total timeouts. Used by callers whose
    /// upstream legitimately needs longer than the 30 s default (AI chat completions).
    static std::tuple<std::string, int64_t> PostWithTimeout(const std::string &url, const std::string &body, const std::unordered_map<std::string, std::string> &headers, long timeoutSeconds);
    static std::tuple<std::string, int64_t> Put(const std::string &url, const std::string &body, const std::unordered_map<std::string, std::string> &headers = {});
    static std::tuple<std::string, int64_t> Get(const std::string &url, const std::unordered_map<std::string, std::string> &headers = {});
    /// Same as Get but with an explicit total timeout. Used by background
    /// pollers (ppServer update check) so shutdown is never held up by a slow
    /// upstream.
    static std::tuple<std::string, int64_t> GetWithTimeout(const std::string &url, long timeoutSeconds, const std::unordered_map<std::string, std::string> &headers = {});
    static std::tuple<std::string, int64_t> Delete(const std::string &url, const std::unordered_map<std::string, std::string> &headers = {});
};
