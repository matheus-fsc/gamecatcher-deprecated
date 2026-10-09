#include "auth.hpp"
#include "store.hpp"
#include "token_store.hpp"

#include <chrono>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

// Exit codes: 1 = erro (rede, sessão, uso); 3 = a Steam recusou algum resgate.
constexpr int kExitClaimFailed = 3;

constexpr const char* kUsage = R"(gamecatcher: resgata jogos pagos que estão grátis na Steam

uso:
  gamecatcher login              login por QR (app Steam) e salva o refresh token
  gamecatcher check              testa a sessão salva (lista quantos jogos a conta tem)
  gamecatcher claim [opções]     busca jogos/DLCs pagos que estão grátis e resgata
      --dry-run                  só lista o que seria resgatado
      --games-only               ignora DLCs
      --force-dlc                tenta DLCs mesmo sem o jogo base (a Steam costuma recusar)
      --cc XX                    país da loja (padrão: o da conta, via cookie steamCountry)
      --sub N                    resgata só o subid N (ex.: brinde visto fora da busca)
  gamecatcher logout             apaga o token salvo
)";

// Carrega o token, gera cookies e persiste o refresh token se a Steam o renovou.
auth::WebCookies session_cookies(auth::Credentials& creds) {
    auth::WebCookies cookies;
    if (auth::web_cookies(creds, cookies)) {
        token_store::save(creds);
        std::cerr << "refresh token renovado\n";
    }
    return cookies;
}

auth::Credentials require_login() {
    auto creds = token_store::load();
    if (!creds) throw std::runtime_error("sem sessão salva (rode `gamecatcher login`)");
    return *creds;
}

int cmd_login() {
    auto creds = auth::login_qr();
    token_store::save(creds);
    std::cout << "logado como " << creds.account_name << " (" << creds.steamid << ")\n"
              << "token salvo em " << token_store::config_dir().string() << "\n";
    return 0;
}

int cmd_check() {
    auto creds = require_login();
    auto cookies = session_cookies(creds);
    auto owned = store::user_data(cookies);
    if (owned.apps.empty() && owned.packages.empty()) {
        std::cerr << "sessão NÃO autenticou na loja, userdata vazio (rode `gamecatcher login`)\n";
        return 1;
    }
    std::cout << "ok: " << creds.account_name << ", " << owned.apps.size() << " apps, "
              << owned.packages.size() << " pacotes\n";
    return 0;
}

struct ClaimOptions {
    bool dry_run = false;
    bool games_only = false;
    bool force_dlc = false;
    std::string country;
    uint32_t subid = 0;
};

std::string format_date(std::time_t t) {
    if (!t) return "?";
    char buf[32];
    std::strftime(buf, sizeof buf, "%d/%m %H:%M", std::localtime(&t));
    return buf;
}

int cmd_claim(const ClaimOptions& opt) {
    // Intervalo entre resgates; a Steam limita ativações (~30 a cada 1,5h).
    constexpr auto kDelay = std::chrono::seconds(3);
    constexpr int kMaxConsecutiveFailures = 3;

    auto creds = require_login();
    auto cookies = session_cookies(creds);
    auto owned = store::user_data(cookies);
    if (owned.apps.empty() && owned.packages.empty())
        throw std::runtime_error("sessão não autenticou na loja (rode `gamecatcher login`)");

    std::vector<store::Promo> promos;
    if (opt.subid) {
        promos.push_back({.subid = opt.subid, .name = "subid " + std::to_string(opt.subid)});
    } else {
        std::string country = !opt.country.empty() ? opt.country : owned.country;
        if (country.empty()) throw std::runtime_error("país da loja desconhecido (use --cc XX)");
        auto appids = store::search_free_promos(opt.games_only);
        promos = store::free_to_keep(appids, country);
        std::cout << appids.size() << " itens na busca, " << promos.size() << " pacotes grátis para manter ("
                  << country << ")\n";
    }

    int claimed = 0, failed = 0, consecutive = 0;
    for (const auto& p : promos) {
        std::string label = p.name + " [" + (p.is_dlc ? "DLC" : "jogo") + ", sub " + std::to_string(p.subid) +
                            ", até " + format_date(p.ends) + "]";
        if (owned.packages.count(p.subid) || (!p.is_dlc && p.appid && owned.apps.count(p.appid))) {
            std::cout << "  já tem   " << label << "\n";
            continue;
        }
        // A Steam recusa DLC cujo jogo base a conta não tem ("Houve um problema ao adicionar...").
        if (p.is_dlc && p.parent_appid && !owned.apps.count(p.parent_appid)) {
            label += " (sem o jogo base " + std::to_string(p.parent_appid) + ")";
            if (!opt.force_dlc) {
                std::cout << "  pulado   " << label << "\n";
                continue;
            }
        }
        if (opt.dry_run) {
            std::cout << "  resgataria " << label << "\n";
            continue;
        }

        if (claimed + failed > 0) std::this_thread::sleep_for(kDelay);
        auto r = store::claim(cookies, p.subid);
        std::cout << (r.ok ? "  OK      " : "  FALHOU  ") << label << "\n            " << r.message << "\n";
        if (r.ok) {
            ++claimed;
            consecutive = 0;
        } else if (++failed, ++consecutive >= kMaxConsecutiveFailures) {
            std::cerr << kMaxConsecutiveFailures << " falhas seguidas, parando (limite de ativações?)\n";
            break;
        }
    }
    if (!opt.dry_run) std::cout << claimed << " resgatados, " << failed << " falhas\n";
    return failed ? kExitClaimFailed : 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    if (argc < 2) {
        std::cout << kUsage;
        return 1;
    }
    std::string cmd = argv[1];
    try {
        if (cmd == "login") return cmd_login();
        if (cmd == "check") return cmd_check();
        if (cmd == "claim") {
            ClaimOptions opt;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--dry-run") opt.dry_run = true;
                else if (a == "--games-only") opt.games_only = true;
                else if (a == "--force-dlc") opt.force_dlc = true;
                else if (a == "--cc" && i + 1 < argc) opt.country = argv[++i];
                else if (a == "--sub" && i + 1 < argc) opt.subid = static_cast<uint32_t>(std::stoul(argv[++i]));
                else throw std::runtime_error("opção desconhecida: " + a);
            }
            return cmd_claim(opt);
        }
        if (cmd == "logout") {
            token_store::remove();
            return 0;
        }
        std::cout << kUsage;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << "\n";
        return 1;
    }
}
