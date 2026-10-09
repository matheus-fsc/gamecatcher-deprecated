#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace http {

using Fields = std::vector<std::pair<std::string, std::string>>;

struct Response {
    long status = 0;
    std::string body;
    std::map<std::string, std::string> headers; // header names lower-cased
    std::string effective_url;
    std::map<std::string, std::string> cookies; // cookies recebidos (Set-Cookie), nome → valor
};

struct Request {
    std::string url;
    std::vector<std::string> headers; // "Name: value"
    std::string cookies;              // "a=1; b=2"
};

// Lança std::runtime_error em falha de transporte (DNS, TLS, timeout).
Response get(const Request& req);
Response post_form(const Request& req, const Fields& fields);      // x-www-form-urlencoded
Response post_multipart(const Request& req, const Fields& fields); // multipart/form-data

std::string url_encode(const std::string& s);

} // namespace http
