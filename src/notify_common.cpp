#include "notify.hpp"

#include <ctime>

namespace notify::detail {

std::string title(const store::Promo& p) { return p.is_dlc ? "DLC grátis na Steam" : "Jogo grátis na Steam"; }

std::string body(const store::Promo& p, const std::string& account) {
    std::string s = p.name;
    if (p.is_dlc && !p.parent_name.empty()) s += "\nDLC de " + p.parent_name + " (precisa do jogo base)";
    if (!p.original_price.empty()) s += "\nDe " + p.original_price + " por R$ 0";
    if (p.ends) {
        char buf[32];
        std::strftime(buf, sizeof buf, "%d/%m às %H:%M", std::localtime(&p.ends));
        s += std::string(p.original_price.empty() ? "\n" : ", ") + "grátis até " + buf;
    }
    if (!account.empty()) s += "\nConta: " + account;
    return s;
}

} // namespace notify::detail
