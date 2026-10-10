#include "notify.hpp"

#include <algorithm>
#include <ctime>
#include <mutex>
#include <thread>

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

namespace notify {

void open_store(uint32_t appid) {
    // A Steam recém-aberta (ainda fazendo login) descarta links steam:// que chegam nesse
    // intervalo. Então: aberturas em fila; se a Steam é jovem, espera ela completar kWarmup.
    constexpr auto kWarmup = std::chrono::seconds(40);
    static std::mutex mu;
    static std::optional<std::chrono::steady_clock::time_point> launched_by_us;
    std::lock_guard lock(mu);

    auto age = detail::steam_age();
    if (launched_by_us) {
        auto ours = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - *launched_by_us);
        age = age ? std::min(*age, ours) : ours; // o pid da Steam pode ainda não ter sido registrado
    }
    if (age && *age < kWarmup) std::this_thread::sleep_for(kWarmup - *age);
    if (!age) launched_by_us = std::chrono::steady_clock::now(); // este clique vai abrir a Steam

    // Sem cliente Steam (nenhum programa para steam://): abre a loja no navegador.
    if (!detail::open_url("steam://store/" + std::to_string(appid)))
        detail::open_url("https://store.steampowered.com/app/" + std::to_string(appid) + "/");
}

} // namespace notify
