#pragma once

#include "auth.hpp"

#include <cstdint>
#include <ctime>
#include <set>
#include <string>
#include <vector>

namespace store {

struct Owned {
    std::set<uint32_t> apps;
    std::set<uint32_t> packages;
    std::string country; // do cookie steamCountry ("BR"), vazio se não veio
};

// Jogos/pacotes da conta. Vazio ⇒ cookies não autenticaram.
// Também guarda em `cookies.extra` os cookies que a loja setar (necessários antes de um POST).
Owned user_data(auth::WebCookies& cookies);

// Appids da busca "pagos que estão grátis" (maxprice=free&specials=1), sem login.
std::vector<uint32_t> search_free_promos(bool games_only);

struct Promo {
    uint32_t appid = 0;
    uint32_t subid = 0;
    std::string name;
    bool is_dlc = false;
    uint32_t parent_appid = 0; // jogo base, se DLC
    std::time_t ends = 0;      // fim do "grátis para manter"
};

// Pacotes "grátis para manter" (100% off) dos apps, via IStoreBrowseService/GetItems.
std::vector<Promo> free_to_keep(const std::vector<uint32_t>& appids, const std::string& country);

struct ClaimResult {
    bool ok = false;
    std::string message; // texto da página de resultado
};

ClaimResult claim(const auth::WebCookies& cookies, uint32_t subid);

} // namespace store
