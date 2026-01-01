#include "webclient.h"
#include <iostream>
#include <sstream>
#include <fmt/format.h>
#include "logger/logger.h"

std::size_t fnWriteData(void *ptr, std::size_t size, std::size_t nmemb, void *stream);

struct ReadData {
    const std::string &data;
    size_t read_pos = 0;
};

static size_t read_callback(char *buffer, size_t size, size_t nItems, void *userdata) {
    auto *readData = static_cast<ReadData *>(userdata);
    size_t bufferSize = size * nItems;
    size_t bytesRemaining = readData->data.size() - readData->read_pos;
    size_t bytesToCopy = std::min(bytesRemaining, bufferSize);
    if (bytesToCopy > 0) {
        std::memcpy(buffer, readData->data.data() + readData->read_pos, bytesToCopy);
        readData->read_pos += bytesToCopy;
    }
    return bytesToCopy;
}

std::tuple<std::string, int64_t>
WebClient::Put(const std::string &url, const std::string &param, const std::unordered_map<std::string, std::string> &headers) {
    ReadData readData {param};
    auto curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &readData);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, param.size());

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
            auto errmsg = fmt::format("curl_easy_perform() failed: {}", curl_easy_strerror(res));
            ShowLog(errmsg);
            throw std::runtime_error(errmsg);
        }
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        curl_easy_cleanup(curl);
        return {out.str(), response_code};
    }
    return {"", 0};
}

/*
// callback function for reading data from a file
static size_t read_callback(char *buffer, size_t size, size_t nmemb, void *userp) {
    FILE *read_file = static_cast<FILE*>(userp);
    size_t bytes_to_read = size * nmemb;
    size_t bytes_read = fread(buffer, 1, bytes_to_read, read_file);
    return bytes_read;
}

//response handling callback
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    static_cast<std::string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
    return size * nmemb;
}

int main() {
    CURL *curl;
    CURLcode res;

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Get a curl easy handle
    curl = curl_easy_init();
    if (curl) {
        const std::string url = "http://example.com/upload/your_file.txt"; // Replace with your target URL
        const std::string filename = "local_file.txt"; // Replace with your local file to upload

        // Open the file to be uploaded
        FILE *file_to_upload = fopen(filename.c_str(), "rb");
        if (!file_to_upload) {
            std::cerr << "Could not open file: " << filename << std::endl;
            return 1;
        }

        // Get the size of the file
        fseek(file_to_upload, 0, SEEK_END);
        long file_size = ftell(file_to_upload);
        fseek(file_to_upload, 0, SEEK_SET);

        // Set the URL for the PUT request
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        // Enable PUT request
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        // Set the read callback function and the data source (file pointer)
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
        curl_easy_setopt(curl, CURLOPT_READDATA, file_to_upload);

        // Set the size of the file to be uploaded
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)file_size);

        // Set up the response handling
        std::string response_buffer;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buffer);

        // Perform the request
        res = curl_easy_perform(curl);

        // Check for errors
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
        } else {
            std::cout << "HTTP PUT successful." << std::endl;
            std::cout << "Server response: " << response_buffer << std::endl;
        }

        // Clean up the curl handle and close the file
        curl_easy_cleanup(curl);
        fclose(file_to_upload);
    } else {
        std::cerr << "Failed to initialize libcurl." << std::endl;
    }

    // Clean up global libcurl resources
    curl_global_cleanup();

    return 0;
}
*/
