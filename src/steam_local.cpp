#include "steam_local.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <regex>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace steam_local {
namespace {

constexpr uint64_t kSteamId64Base = 76561197960265728ULL;

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

#ifdef _WIN32
std::optional<DWORD> registry_dword(const wchar_t* key, const wchar_t* value) {
    DWORD data = 0, size = sizeof data;
    if (RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size) != ERROR_SUCCESS)
        return std::nullopt;
    return data;
}
#endif

} // namespace

std::optional<fs::path> steam_root() {
    std::vector<fs::path> roots;
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD size = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf,
                     &size) == ERROR_SUCCESS)
        roots.emplace_back(buf);
    roots.emplace_back(L"C:\\Program Files (x86)\\Steam");
#else
    if (const char* home = std::getenv("HOME")) {
        roots.emplace_back(fs::path(home) / ".local" / "share" / "Steam");
        roots.emplace_back(fs::path(home) / ".steam" / "steam");
    }
#endif
    std::error_code ec;
    for (const auto& root : roots)
        if (fs::exists(packageinfo_path(root), ec)) return root;
    return std::nullopt;
}

fs::path packageinfo_path(const fs::path& root) { return root / "appcache" / "packageinfo.vdf"; }

fs::path licensecache_path(const fs::path& root, uint32_t account_id) {
    return root / "userdata" / std::to_string(account_id) / "config" / "licensecache";
}

Owned read_owned(const fs::path& packageinfo) {
    const std::string data = read_file(packageinfo);

    auto u32 = [&](size_t at) {
        uint32_t v = 0;
        for (int b = 0; b < 4; ++b) v |= static_cast<uint32_t>(static_cast<uint8_t>(data[at + b])) << (8 * b);
        return v;
    };

    // KeyValues binário: tipo 0x00 = subseção, 0x02 = int32, 0x08 = fim da subseção.
    // Cada pacote tem o campo int32 "packageid" e a subseção "appids" { "0": appid, "1": ... }.
    Owned owned;
    static const std::string package_key("\x02packageid\0", 11);
    for (size_t i = data.find(package_key); i != std::string::npos; i = data.find(package_key, i + 1))
        if (i + package_key.size() + 4 <= data.size()) owned.packages.insert(u32(i + package_key.size()));

    // "\0" octal: com "\x00a" o 'a' viraria parte do escape hexadecimal.
    static const std::string apps_key("\0appids\0", 8);
    for (size_t i = data.find(apps_key); i != std::string::npos; i = data.find(apps_key, i + 1)) {
        size_t j = i + apps_key.size();
        while (j < data.size() && data[j] == '\x02') { // entradas int32 até o 0x08
            size_t name_end = data.find('\0', j + 1);
            if (name_end == std::string::npos || name_end + 5 > data.size()) break;
            owned.apps.insert(u32(name_end + 1));
            j = name_end + 5;
        }
    }
    return owned;
}

std::vector<Account> accounts(const fs::path& root) {
    // loginusers.vdf (texto): "users" { "<SteamID64>" { "AccountName" "x" "Timestamp" "123" ... } }
    const std::string data = read_file(root / "config" / "loginusers.vdf");
    static const std::regex user_re(R"re("(7656\d{13})"\s*\{([^}]*)\})re");
    static const std::regex name_re(R"re("AccountName"\s*"([^"]*)")re");
    static const std::regex time_re(R"re("Timestamp"\s*"(\d+)")re");

    std::vector<Account> out;
    for (std::sregex_iterator it(data.begin(), data.end(), user_re), end; it != end; ++it) {
        const std::string block = (*it)[2];
        Account a;
        a.id = static_cast<uint32_t>(std::stoull((*it)[1]) - kSteamId64Base);
        std::smatch m;
        if (std::regex_search(block, m, name_re)) a.name = m[1];
        if (std::regex_search(block, m, time_re)) a.timestamp = std::stoull(m[1]);
        out.push_back(std::move(a));
    }
    return out;
}

std::optional<Account> active_account(const fs::path& root) {
    auto all = accounts(root);
    if (all.empty()) return std::nullopt;
#ifdef _WIN32
    if (auto active = registry_dword(L"Software\\Valve\\Steam\\ActiveProcess", L"ActiveUser"); active && *active)
        for (const auto& a : all)
            if (a.id == *active) return a;
#endif
    // Linux não expõe a conta ativa; o login mais recente é a conta em uso.
    return *std::max_element(all.begin(), all.end(), [](auto& a, auto& b) { return a.timestamp < b.timestamp; });
}

} // namespace steam_local
