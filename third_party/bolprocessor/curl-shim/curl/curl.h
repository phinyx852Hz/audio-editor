/* PHI: BP3 posts notes to its PHP interface with libcurl; PHI has no such interface, so these do nothing. */
#pragma once
typedef void CURL;
typedef int CURLcode;
#define CURLE_OK 0
#define CURL_GLOBAL_DEFAULT 0
#define CURLOPT_URL 0
#define CURLOPT_HTTPGET 0
#define CURLOPT_TIMEOUT_MS 0
static inline CURLcode curl_global_init (long f) { (void) f; return 0; }
static inline CURL* curl_easy_init (void) { return 0; }
static inline char* curl_easy_escape (CURL* c, const char* s, int n) { (void) c; (void) s; (void) n; return 0; }
static inline void curl_easy_cleanup (CURL* c) { (void) c; }
#define curl_easy_setopt(c, o, ...) ((void) (c), 0)
static inline CURLcode curl_easy_perform (CURL* c) { (void) c; return 1; }
static inline const char* curl_easy_strerror (CURLcode e) { (void) e; return "no network in PHI"; }
static inline void curl_free (void* p) { (void) p; }
static inline void curl_global_cleanup (void) {}
