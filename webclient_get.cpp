#include "webclient.h"
#include <iostream>
#include <sstream>
#include <fmt/format.h>
#include "logger.h"

std::size_t fnWriteData(void *ptr, std::size_t size, std::size_t nmemb, void *stream);

std::tuple<std::string, int64_t> WebClient::Get(const std::string &url, const std::unordered_map<std::string, std::string> &headers) {
    auto curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);  // Prevent "longjmp causes uninitialized stack frame" bug
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "deflate");
        std::stringstream out;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fnWriteData);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

        curl_slist *hdr = nullptr;
        for (const auto &header : headers) {
            auto headerStr = fmt::format("{}: {}", header.first, header.second);
            hdr = curl_slist_append(hdr, headerStr.c_str());
        }
        if (hdr) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr);

        /* Perform the request, res will get the return code */
        CURLcode res = curl_easy_perform(curl);
        if (hdr) curl_slist_free_all(hdr);
        /* Check for errors */
        if (res != CURLE_OK) {
            auto errmsg = fmt::format("curl_easy_perform() failed: {}", curl_easy_strerror(res));
            ShowLog(errmsg);
            throw errmsg;
        }
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_cleanup(curl);
        return {out.str(), response_code};
    }
    return {"", 0};
}
