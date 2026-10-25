#include "webclient.h"
#include "cacert.h"
#include <iostream>
#include <sstream>
#include <fmt/format.h>
#include "logger/logger.h"

std::size_t fnWriteData(void *ptr, std::size_t size, std::size_t nmemb, void *stream);

std::tuple<std::string, int64_t> WebClient::Delete(const std::string &url,
                                                   const std::unordered_map<std::string, std::string> &headers) {
    auto curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &WebClientCA::GetCACertBlob());
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "deflate");

        curl_slist *hdr = nullptr;
        for (const auto &header : headers) {
            auto headerStr = fmt::format("{}: {}", header.first, header.second);
            hdr = curl_slist_append(hdr, headerStr.c_str());
        }
        if (hdr) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);

        std::stringstream out;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fnWriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        CURLcode res = curl_easy_perform(curl);
        if (hdr) curl_slist_free_all(hdr);
        if (res != CURLE_OK) {
            std::string errmsg = curl_easy_strerror(res);
            LOG_ERROR("curl DELETE failed: {}: url: {}", errmsg, url);
            throw std::runtime_error(errmsg);
        }
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_cleanup(curl);
        return {out.str(), response_code};
    }
    return {"", 0};
}
