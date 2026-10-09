#include "auth.hpp"

#include "base64.hpp"
#include "http.hpp"
#include "protobuf.hpp"

#include <nlohmann/json.hpp>
#include <qrcodegen.hpp>

#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
#include <thread>

namespace auth {
namespace {

constexpr int kPlatformMobileApp = 3;   // EAuthTokenPlatformType
constexpr int kOsAndroidUnknown = -500; // EOSType
constexpr int kRenewalAllow = 1;        // ETokenRenewalType
constexpr int kEResultOK = 1;

// Cabeçalhos que o steam-session usa para se passar pelo app mobile.
const std::vector<std::string> kMobileHeaders = {
    "User-Agent: okhttp/4.9.2",
    "Cookie: mobileClient=android; mobileClientVersion=777777 3.10.3",
    "Accept: application/json, text/plain, */*",
};

pb::Message call(const std::string& method, const pb::Writer& body) {
    http::Request req{
        .url = "https://api.steampowered.com/IAuthenticationService/" + method + "/v1/",
        .headers = kMobileHeaders,
        .cookies = {},
    };
    auto res = http::post_multipart(req, {{"input_protobuf_encoded", b64::encode(body.data())}});
    if (res.status < 200 || res.status >= 300)
        throw std::runtime_error(method + ": HTTP " + std::to_string(res.status));

    auto it = res.headers.find("x-eresult");
    int eresult = it == res.headers.end() ? kEResultOK : std::stoi(it->second);
    if (eresult != kEResultOK) {
        auto msg = res.headers.find("x-error_message");
        throw std::runtime_error(method + ": EResult " + std::to_string(eresult) +
                                 (msg == res.headers.end() ? "" : " (" + msg->second + ")"));
    }
    return pb::parse(res.body);
}

nlohmann::json jwt_payload(const std::string& token) {
    auto a = token.find('.');
    auto b = token.find('.', a + 1);
    if (a == std::string::npos || b == std::string::npos) throw std::runtime_error("JWT malformado");
    return nlohmann::json::parse(b64::decode(token.substr(a + 1, b - a - 1)));
}

void print_qr(const std::string& text) {
    using qrcodegen::QrCode;
    const QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::LOW);
    const int border = 2;
    auto dark = [&](int x, int y) { return qr.getModule(x, y); }; // fora dos limites → false
    // Dois módulos por caractere (meio-bloco), preto sobre branco para funcionar em tema claro e escuro.
    for (int y = -border; y < qr.getSize() + border; y += 2) {
        std::cout << "\x1b[30;47m";
        for (int x = -border; x < qr.getSize() + border; ++x) {
            bool top = dark(x, y), bottom = dark(x, y + 1);
            std::cout << (top && bottom ? "█" : top ? "▀" : bottom ? "▄" : " ");
        }
        std::cout << "\x1b[0m\n";
    }
}

std::string random_hex(size_t nbytes) {
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::string out;
    for (size_t i = 0; i < nbytes; ++i) {
        auto b = static_cast<unsigned>(rd() & 0xFF);
        out += hex[b >> 4];
        out += hex[b & 0xF];
    }
    return out;
}

} // namespace

Credentials login_qr() {
    pb::Writer device;
    device.bytes(1, "Galaxy S25")
        .varint(2, kPlatformMobileApp)
        .int32(3, kOsAndroidUnknown)
        .varint(4, 528);
    auto begin = call("BeginAuthSessionViaQR", pb::Writer{}.message(3, device));

    uint64_t client_id = begin.u64(1);
    std::string challenge_url = begin.str(2);
    std::string request_id = begin.str(3);
    float interval = begin.f32(4, 5.0f);
    if (challenge_url.empty()) throw std::runtime_error("BeginAuthSessionViaQR sem challenge_url");

    std::cout << "Escaneie com o app Steam (Steam Guard → ícone de QR):\n\n";
    print_qr(challenge_url);
    std::cout << "\n" << challenge_url << "\n\nAguardando aprovação..." << std::endl;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(interval * 1000)));

        auto poll = call("PollAuthSessionStatus", pb::Writer{}.varint(1, client_id).bytes(2, request_id));

        if (uint64_t new_id = poll.u64(1)) client_id = new_id;
        if (auto url = poll.str(2); !url.empty()) {
            // O QR expira e a Steam manda outro.
            std::cout << "\nQR renovado:\n\n";
            print_qr(url);
        }
        if (auto refresh = poll.str(3); !refresh.empty()) {
            Credentials c;
            c.refresh_token = refresh;
            c.account_name = poll.str(6);
            c.steamid = jwt_payload(refresh).at("sub").get<std::string>();
            return c;
        }
    }
    throw std::runtime_error("tempo esgotado esperando aprovação do QR");
}

bool web_cookies(Credentials& creds, WebCookies& out) {
    auto resp = call("GenerateAccessTokenForApp",
                     pb::Writer{}
                         .bytes(1, creds.refresh_token)
                         .fixed64(2, std::stoull(creds.steamid))
                         .varint(3, kRenewalAllow));

    std::string access = resp.str(1);
    if (access.empty()) throw std::runtime_error("GenerateAccessTokenForApp sem access_token");

    out.steam_login_secure = http::url_encode(creds.steamid + "||" + access);
    out.sessionid = random_hex(12);

    std::string renewed = resp.str(2);
    if (!renewed.empty() && renewed != creds.refresh_token) {
        creds.refresh_token = renewed;
        return true;
    }
    return false;
}

} // namespace auth
