#pragma once

#include <curl/curl.h>

namespace WebClientCA {

// Pre-loaded Mozilla CA certificate bundle for SSL verification.
// Returns a curl_blob that can be passed to CURLOPT_CAINFO_BLOB.
// Safe to call before any curl_easy_perform() — the data is statically embedded.
const curl_blob& GetCACertBlob();

}  // namespace WebClientCA
