#pragma once

// Leitura (só leitura) dos arquivos do cliente Steam instalado. Nada aqui fala com a rede.
//
// Arquivos usados:
//   appcache/packageinfo.vdf             cache de pacotes do PC inteiro: junta as licenças de
//                                        todas as contas que já entraram neste cliente.
//   config/loginusers.vdf                contas que já entraram neste cliente.
//   userdata/<conta>/config/licensecache licenças de UMA conta (conteúdo cifrado; só usamos a
//                                        data de modificação, que muda quando a conta recebe
//                                        uma licença nova).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace steam_local {

// Pasta de instalação do cliente. Linux: ~/.local/share/Steam ou ~/.steam/steam;
// Windows: SteamPath do registro (HKCU\Software\Valve\Steam).
std::optional<std::filesystem::path> steam_root();

struct Owned {
    std::set<uint32_t> packages; // licenças de qualquer conta deste PC
    std::set<uint32_t> apps;     // apps liberados por essas licenças
};

std::filesystem::path packageinfo_path(const std::filesystem::path& root);

// O cliente grava o arquivo segundos depois de receber uma licença nova; só abrir a página da
// loja não grava nada.
Owned read_owned(const std::filesystem::path& packageinfo);

struct Account {
    uint32_t id = 0; // account id (SteamID64 - 76561197960265728), nome da pasta em userdata/
    std::string name;
    uint64_t timestamp = 0; // último login neste cliente
};

std::vector<Account> accounts(const std::filesystem::path& root);

// Conta logada agora (Windows: ActiveProcess\ActiveUser) ou, sem essa informação, a do login
// mais recente.
std::optional<Account> active_account(const std::filesystem::path& root);

std::filesystem::path licensecache_path(const std::filesystem::path& root, uint32_t account_id);

} // namespace steam_local
