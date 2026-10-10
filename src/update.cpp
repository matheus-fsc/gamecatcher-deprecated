#include "update.hpp"

#include "http.hpp"
#include "install.hpp"
#include "seen_db.hpp"

#include <monocypher-ed25519.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace update {
namespace {

constexpr const char* kLatest = "https://api.github.com/repos/matheus-fsc/gamecatcher/releases/latest";

#ifdef _WIN32
constexpr const char* kPlatform = "windows-x64.exe";
#else
constexpr const char* kPlatform = "linux-x86_64";
#endif

// Chave pública Ed25519 das releases (a privada é o segredo RELEASE_SIGNING_KEY do CI).
constexpr std::array<uint8_t, 32> kPublicKey = {
    0xb7, 0x25, 0xc2, 0xee, 0xd9, 0x35, 0xfe, 0x28, 0xb9, 0x54, 0x55, 0xab, 0x73, 0x6c, 0x9b, 0x62,
    0x23, 0x59, 0x3a, 0x0a, 0x7f, 0x8c, 0x44, 0xbe, 0xc3, 0xb6, 0x5a, 0xb7, 0x16, 0xbc, 0xc1, 0xcb,
};

// "1.10.2" -> {1, 10, 2}; partes faltando valem 0.
std::array<unsigned long, 3> parse(const std::string& v) {
    std::array<unsigned long, 3> out{};
    size_t pos = 0;
    for (auto& part : out) {
        if (pos >= v.size()) break;
        size_t end = v.find('.', pos);
        part = std::stoul(v.substr(pos, end - pos));
        pos = end == std::string::npos ? v.size() : end + 1;
    }
    return out;
}

http::Response fetch(const std::string& url, const char* accept) {
    auto res = http::get({.url = url, .headers = {std::string("Accept: ") + accept}, .cookies = {}});
    if (res.status != 200) throw std::runtime_error(url + ": HTTP " + std::to_string(res.status));
    return res;
}

std::filesystem::path stamp() { return seen_db::Db::default_path().parent_path() / "update-check"; }

} // namespace

bool verify(const std::string& asset, const std::string& binary, const std::string& sig) {
    if (sig.size() != 64) return false;
    const std::string message = asset + "\n" + binary; // o nome amarra a versão e a plataforma
    return crypto_ed25519_check(reinterpret_cast<const uint8_t*>(sig.data()), kPublicKey.data(),
                                reinterpret_cast<const uint8_t*>(message.data()), message.size()) == 0;
}

std::optional<Release> check() {
    // GAMECATCHER_UPDATE_URL: só para testes (servidor local); a assinatura é conferida igual.
    const char* url = std::getenv("GAMECATCHER_UPDATE_URL");
    auto j = nlohmann::json::parse(fetch(url && *url ? url : kLatest, "application/vnd.github+json").body);
    std::string tag = j.value("tag_name", "");
    Release r;
    r.version = tag.rfind('v', 0) == 0 ? tag.substr(1) : tag;
    if (r.version.empty() || parse(r.version) <= parse(GAMECATCHER_VERSION)) return std::nullopt;

    r.asset = "gamecatcher-" + r.version + "-" + kPlatform;
    for (const auto& a : j.value("assets", nlohmann::json::array())) {
        const std::string name = a.value("name", "");
        if (name == r.asset) r.binary_url = a.value("browser_download_url", "");
        if (name == r.asset + ".sig") r.sig_url = a.value("browser_download_url", "");
    }
    if (r.binary_url.empty() || r.sig_url.empty()) return std::nullopt; // release sem binário assinado
    return r;
}

void apply(const Release& r) {
    const std::string binary = fetch(r.binary_url, "application/octet-stream").body;
    const std::string sig = fetch(r.sig_url, "application/octet-stream").body;
    if (!verify(r.asset, binary, sig)) throw std::runtime_error("assinatura inválida em " + r.asset + ": nada foi trocado");
    install::replace_installed(binary);
}

bool due() {
    std::error_code ec;
    auto checked = std::filesystem::last_write_time(stamp(), ec);
    if (!ec && std::filesystem::file_time_type::clock::now() - checked < std::chrono::hours(24)) return false;
    std::filesystem::create_directories(stamp().parent_path(), ec);
    std::ofstream(stamp(), std::ios::trunc) << GAMECATCHER_VERSION; // a data de modificação é a marca
    return true;
}

} // namespace update
