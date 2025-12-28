#include <curl/curl.h>
#include <string>

class WebClient {
public:
    static std::tuple<std::string, int64_t> Post(const std::string &url, const std::string &body, const std::unordered_map<std::string, std::string> &headers = {});
    static std::tuple<std::string, int64_t> Put(const std::string &url, const std::string &body, const std::unordered_map<std::string, std::string> &headers = {});
    static std::tuple<std::string, int64_t> Get(const std::string &url, const std::unordered_map<std::string, std::string> &headers = {});
};
