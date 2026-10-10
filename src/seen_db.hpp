#pragma once

// Banco local das promoções já tratadas (resgatadas ou ignoradas), por conta Steam.
//
// Formato binário compacto (versão 1):
//   "GCDB" | versão (1 byte) | nº de contas (varint)
//   por conta: account id (varint) | nº de entradas (varint) |
//              entradas em ordem crescente de subid: varint((subid - anterior) << 1 | resgatado)
// Subids próximos ocupam ~2 bytes por entrada.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>

namespace seen_db {

enum class Status : uint8_t { Ignored = 0, Claimed = 1 };

// Conta 0 = sem cliente Steam no PC (não dá para saber a conta).
using Entries = std::map<uint32_t, std::map<uint32_t, Status>>; // conta -> subid -> status

class Db {
public:
    // Linux: $XDG_STATE_HOME/gamecatcher/seen.db (ou ~/.local/state/...)
    // Windows: %LOCALAPPDATA%\gamecatcher\seen.db
    static std::filesystem::path default_path();

    explicit Db(std::filesystem::path path);

    std::optional<Status> get(uint32_t account, uint32_t subid) const;
    void set(uint32_t account, uint32_t subid, Status status); // grava no disco na hora (troca atômica)
    const Entries& entries() const { return entries_; }
    void clear();

private:
    void save() const;

    std::filesystem::path path_;
    Entries entries_;
};

} // namespace seen_db
