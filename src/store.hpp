#pragma once

// Consultas públicas à loja Steam, sem login.

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace store {

struct SearchResult {
    std::vector<uint32_t> appids;
    std::string country; // do cookie steamCountry (geolocalização por IP), ex.: "BR"
};

// Appids da busca "pagos que estão grátis" (maxprice=free&specials=1).
SearchResult search_free_promos(bool games_only);

struct Promo {
    uint32_t appid = 0;
    uint32_t subid = 0;
    std::string name;
    bool is_dlc = false;
    uint32_t parent_appid = 0;  // jogo base, se DLC
    std::string parent_name;
    std::string original_price; // ex.: "R$ 28,82"
    std::time_t ends = 0;       // fim do "grátis para manter"
};

// Pacotes "grátis para manter" (100% off) dos apps, via IStoreBrowseService/GetItems.
std::vector<Promo> free_to_keep(const std::vector<uint32_t>& appids, const std::string& country);

} // namespace store
