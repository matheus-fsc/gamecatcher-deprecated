#include "token_store.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace token_store {
namespace {

fs::path token_file() { return config_dir() / "credentials.json"; }

} // namespace

fs::path config_dir() {
#ifdef _WIN32
    if (const char* appdata = std::getenv("APPDATA")) return fs::path(appdata) / "gamecatcher";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return fs::path(xdg) / "gamecatcher";
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".config" / "gamecatcher";
#endif
    throw std::runtime_error("não foi possível determinar o diretório de configuração");
}

std::optional<auth::Credentials> load() {
    std::ifstream in(token_file());
    if (!in) return std::nullopt;
    auto j = nlohmann::json::parse(in);
    return auth::Credentials{
        .steamid = j.at("steamid"),
        .account_name = j.value("account_name", ""),
        .refresh_token = j.at("refresh_token"),
    };
}

void save(const auth::Credentials& creds) {
    fs::create_directories(config_dir());
    // TODO(windows): cifrar com DPAPI (CryptProtectData) em vez de depender só da ACL do %APPDATA%.
    auto path = token_file();
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) throw std::runtime_error("não foi possível escrever " + tmp.string());
        fs::permissions(tmp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
        out << nlohmann::json{
                   {"steamid", creds.steamid},
                   {"account_name", creds.account_name},
                   {"refresh_token", creds.refresh_token},
               }
                   .dump(2);
    }
    fs::rename(tmp, path); // troca atômica: nunca perder o token se o processo morrer no meio
}

void remove() { fs::remove(token_file()); }

} // namespace token_store
