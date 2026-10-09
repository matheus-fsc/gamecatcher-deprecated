#include "http.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace http {
namespace {

constexpr const char* kUserAgent =
    "Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0";

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

size_t write_body(char* ptr, size_t size, size_t n, void* user) {
    static_cast<std::string*>(user)->append(ptr, size * n);
    return size * n;
}

size_t write_header(char* ptr, size_t size, size_t n, void* user) {
    std::string line(ptr, size * n);
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::string value = line.substr(colon + 1);
        auto first = value.find_first_not_of(" \t");
        auto last = value.find_last_not_of(" \t\r\n");
        value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
        static_cast<std::map<std::string, std::string>*>(user)->insert_or_assign(name, value);
    }
    return size * n;
}

using CurlPtr = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using SlistPtr = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
using MimePtr = std::unique_ptr<curl_mime, decltype(&curl_mime_free)>;

CurlPtr make_handle(const Request& req, Response& res, SlistPtr& hdrs) {
    static CurlGlobal global;
    CurlPtr curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) throw std::runtime_error("curl_easy_init falhou");

    for (const auto& h : req.headers) hdrs.reset(curl_slist_append(hdrs.release(), h.c_str()));

    curl_easy_setopt(curl.get(), CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, hdrs.get());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, ""); // gzip/br se disponível
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &res.body);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &res.headers);
    if (!req.cookies.empty()) curl_easy_setopt(curl.get(), CURLOPT_COOKIE, req.cookies.c_str());
    // Liga o cookie engine (em memória): a loja seta steamCountry num 302 para a mesma URL
    // e entra em loop se o cookie não for reenviado.
    curl_easy_setopt(curl.get(), CURLOPT_COOKIEFILE, "");
    if (std::getenv("GAMECATCHER_DEBUG")) curl_easy_setopt(curl.get(), CURLOPT_VERBOSE, 1L);
    return curl;
}

void perform(CURL* curl, Response& res) {
    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) throw std::runtime_error(std::string("HTTP: ") + curl_easy_strerror(rc));
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status);
    char* eff = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff) res.effective_url = eff;

    // Formato Netscape: domain \t flag \t path \t secure \t expires \t name \t value
    curl_slist* list = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_COOKIELIST, &list) == CURLE_OK) {
        for (auto* node = list; node; node = node->next) {
            std::string line = node->data;
            size_t pos = 0;
            for (int tabs = 0; tabs < 5 && pos != std::string::npos; ++tabs) pos = line.find('\t', pos) + 1;
            auto sep = line.find('\t', pos);
            if (pos == 0 || sep == std::string::npos) continue;
            res.cookies.insert_or_assign(line.substr(pos, sep - pos), line.substr(sep + 1));
        }
        curl_slist_free_all(list);
    }
}

} // namespace

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

Response get(const Request& req) {
    Response res;
    SlistPtr hdrs(nullptr, curl_slist_free_all);
    auto curl = make_handle(req, res, hdrs);
    perform(curl.get(), res);
    return res;
}

Response post_form(const Request& req, const Fields& fields) {
    std::string body;
    for (const auto& [k, v] : fields) {
        if (!body.empty()) body += '&';
        body += url_encode(k) + '=' + url_encode(v);
    }
    Response res;
    SlistPtr hdrs(nullptr, curl_slist_free_all);
    auto curl = make_handle(req, res, hdrs);
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    perform(curl.get(), res);
    return res;
}

Response post_multipart(const Request& req, const Fields& fields) {
    Response res;
    SlistPtr hdrs(nullptr, curl_slist_free_all);
    auto curl = make_handle(req, res, hdrs);
    MimePtr mime(curl_mime_init(curl.get()), curl_mime_free);
    for (const auto& [k, v] : fields) {
        curl_mimepart* part = curl_mime_addpart(mime.get());
        curl_mime_name(part, k.c_str());
        curl_mime_data(part, v.data(), v.size());
    }
    curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, mime.get());
    perform(curl.get(), res);
    return res;
}

} // namespace http
