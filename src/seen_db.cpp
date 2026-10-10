#include "seen_db.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace seen_db {
namespace {

constexpr char kMagic[4] = {'G', 'C', 'D', 'B'};
constexpr uint8_t kVersion = 1;

void put_varint(std::string& out, uint64_t v) {
    while (v >= 0x80) {
        out += static_cast<char>((v & 0x7F) | 0x80);
        v >>= 7;
    }
    out += static_cast<char>(v);
}

uint64_t get_varint(const std::string& in, size_t& i) {
    uint64_t v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (i >= in.size()) throw std::runtime_error("seen.db truncado");
        auto b = static_cast<uint8_t>(in[i++]);
        v |= static_cast<uint64_t>(b & 0x7F) << shift;
        if (!(b & 0x80)) return v;
    }
    throw std::runtime_error("seen.db corrompido");
}

} // namespace

fs::path Db::default_path() {
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA")) return fs::path(local) / "gamecatcher" / "seen.db";
#else
    if (const char* xdg = std::getenv("XDG_STATE_HOME"); xdg && *xdg) return fs::path(xdg) / "gamecatcher" / "seen.db";
    if (const char* home = std::getenv("HOME")) return fs::path(home) / ".local" / "state" / "gamecatcher" / "seen.db";
#endif
    throw std::runtime_error("não foi possível determinar onde guardar o seen.db");
}

Db::Db(fs::path path) : path_(std::move(path)) {
    std::ifstream f(path_, std::ios::binary);
    if (!f) return; // primeira execução
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    if (data.size() < 5 || data.compare(0, 4, kMagic, 4) != 0)
        throw std::runtime_error(path_.string() + " não é um seen.db");
    if (static_cast<uint8_t>(data[4]) != kVersion)
        throw std::runtime_error(path_.string() + ": versão desconhecida");

    size_t i = 5;
    for (uint64_t accounts = get_varint(data, i); accounts > 0; --accounts) {
        auto& subs = entries_[static_cast<uint32_t>(get_varint(data, i))];
        uint64_t subid = 0;
        for (uint64_t n = get_varint(data, i); n > 0; --n) {
            uint64_t v = get_varint(data, i);
            subid += v >> 1;
            subs[static_cast<uint32_t>(subid)] = static_cast<Status>(v & 1);
        }
    }
}

std::optional<Status> Db::get(uint32_t account, uint32_t subid) const {
    auto acc = entries_.find(account);
    if (acc == entries_.end()) return std::nullopt;
    auto it = acc->second.find(subid);
    if (it == acc->second.end()) return std::nullopt;
    return it->second;
}

void Db::set(uint32_t account, uint32_t subid, Status status) {
    entries_[account][subid] = status;
    save();
}

void Db::clear() {
    entries_.clear();
    save();
}

void Db::save() const {
    std::string out(kMagic, 4);
    out += static_cast<char>(kVersion);
    put_varint(out, entries_.size());
    for (const auto& [account, subs] : entries_) {
        put_varint(out, account);
        put_varint(out, subs.size());
        uint32_t prev = 0;
        for (const auto& [subid, status] : subs) { // std::map: já em ordem crescente
            put_varint(out, (static_cast<uint64_t>(subid - prev) << 1) | static_cast<uint8_t>(status));
            prev = subid;
        }
    }

    fs::create_directories(path_.parent_path());
    auto tmp = path_;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("não foi possível escrever " + tmp.string());
        f.write(out.data(), static_cast<std::streamsize>(out.size()));
    }
    fs::rename(tmp, path_);
}

} // namespace seen_db
