// PharmaPOS — WebClient::PostWithTimeout
//
// Identical to WebClient::Post except the caller chooses the timeouts. The
// default Post() gives every upstream 30 s, which is fine for payment/REST
// providers but too short for AI chat completions.

#include "webclient.h"
#include "cacert.h"
#include <sstream>
#include <stdexcept>
#include <fmt/format.h>
#include "logger/logger.h"

namespace {
std::size_t WriteData(void *ptr, std::size_t size, std::size_t nmemb, void *stream) {
    std::string data((const char *)ptr, (std::size_t)size * nmemb);
    *((std::stringstream *)stream) << data;
    return size * nmemb;
}
}  // namespace

std::tuple<std::string, int64_t> WebClient::PostWithTimeout(
    const std::string &url,
    const std::string &param,
    const std::unordered_map<std::string, std::string> &headers,
    long timeoutSeconds) {
    auto curl = curl_easy_init();
    if (!curl) return {"", 0};

    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &WebClientCA::GetCACertBlob());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(param.length()));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, param.c_str());

    curl_slist *hdr = nullptr;
    for (const auto &header : headers) {
        auto headerStr = fmt::format("{}: {}", header.first, header.second);
        hdr = curl_slist_append(hdr, headerStr.c_str());
    }
    if (hdr) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);

    const long total = timeoutSeconds > 0 ? timeoutSeconds : 30L;
    const long connect = total < 10L ? total : 10L;
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);  // Prevent "longjmp causes uninitialized stack frame"
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, connect);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, total);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "deflate");

    std::stringstream out;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    CURLcode res = curl_easy_perform(curl);
    if (hdr) curl_slist_free_all(hdr);

    if (res != CURLE_OK) {
        auto errmsg = fmt::format("curl_easy_perform() failed: {}", curl_easy_strerror(res));
        LOG_INFO(errmsg);
        curl_easy_cleanup(curl);
        throw std::runtime_error(errmsg);
    }

    long responseCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);
    return {out.str(), responseCode};
}
