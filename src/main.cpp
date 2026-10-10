#include "install.hpp"
#include "notify.hpp"
#include "seen_db.hpp"
#include "steam_local.hpp"
#include "store.hpp"
#include "update.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <mutex>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#endif

namespace {

constexpr const char* kUsage = R"(gamecatcher: avisa quando um jogo pago fica grátis na Steam (sem login)

uso:
  gamecatcher [run] [opções]   busca promoções e mostra uma notificação para cada uma nova
                               (Resgatar: abre na Steam; Ignorar: não avisa mais)
  gamecatcher list [opções]    só lista as promoções e o status no banco local
  gamecatcher db               mostra o banco local
  gamecatcher db clear         apaga o banco local (tudo volta a ser avisado)
  gamecatcher install          (Linux) instala para o usuário atual e agenda uma verificação no
                               login e a cada 6 horas; no Windows, use o install.bat do pacote
  gamecatcher uninstall        (Linux) remove o programa e o agendamento (o banco local fica);
                               no Windows, use o uninstall.bat do pacote
  gamecatcher update           Linux: baixa e instala a versão mais nova (assinatura conferida);
                               Windows: abre a página da versão nova (instale pelo install.bat);
                               o programa instalado também avisa numa notificação, 1x por dia

opções:
  --games-only                 ignora DLCs (por padrão, avisa DLCs cujo jogo base você tem)
  --cc XX                      país da loja (padrão: pelo IP)
  --max N                      no máximo N notificações por execução (padrão 5)
  --version                    mostra a versão
)";

struct Options {
    bool games_only = false;
    std::string country;
    size_t max = 5;
};

std::vector<store::Promo> find_promos(const Options& opt) {
    auto search = store::search_free_promos(opt.games_only);
    std::string country = !opt.country.empty() ? opt.country : search.country;
    if (country.empty()) throw std::runtime_error("país da loja desconhecido (use --cc XX)");
    auto promos = store::free_to_keep(search.appids, country);
    // Um app pode ter mais de um pacote grátis; basta avisar uma vez por app.
    std::sort(promos.begin(), promos.end(), [](auto& a, auto& b) { return a.appid < b.appid; });
    promos.erase(std::unique(promos.begin(), promos.end(), [](auto& a, auto& b) { return a.appid == b.appid; }),
                 promos.end());
    return promos;
}

const char* status_label(std::optional<seen_db::Status> s) {
    if (!s) return "novo     ";
    return *s == seen_db::Status::Claimed ? "resgatado" : "ignorado ";
}

// No login da sessão a rede (Wi-Fi, VPN) pode ainda não estar pronta: tenta mais algumas vezes.
std::vector<store::Promo> find_promos_retrying(const Options& opt) {
    constexpr int kAttempts = 4;
    constexpr auto kWait = std::chrono::seconds(30);
    for (int attempt = 1;; ++attempt) {
        try {
            return find_promos(opt);
        } catch (const std::exception& e) {
            if (attempt == kAttempts) throw;
            std::cerr << "falhou (" << e.what() << "), tentando de novo em 30 s\n";
            std::this_thread::sleep_for(kWait);
        }
    }
}

// O que o cliente Steam deste PC diz sobre as contas e licenças.
struct Local {
    std::optional<std::filesystem::path> root; // sem valor: cliente Steam não instalado
    std::vector<steam_local::Account> accounts;
    std::optional<steam_local::Account> active;
    steam_local::Owned owned; // de TODAS as contas do PC (o packageinfo.vdf é compartilhado)

    static Local load() {
        Local l;
        l.root = steam_local::steam_root();
        if (!l.root) return l;
        l.accounts = steam_local::accounts(*l.root);
        l.active = steam_local::active_account(*l.root);
        l.owned = steam_local::read_owned(steam_local::packageinfo_path(*l.root));
        return l;
    }

    // Com mais de uma conta, o packageinfo.vdf mistura as licenças de todas: dá para saber que
    // nenhuma conta tem algo, mas não QUAL conta tem.
    bool shared() const { return accounts.size() > 1; }
    uint32_t account_id() const { return active ? active->id : 0; }
    std::string account_name() const { return active ? active->name : std::string{}; }
    std::string name_of(uint32_t id) const {
        for (const auto& a : accounts)
            if (a.id == id) return a.name;
        return id ? std::to_string(id) : "(sem cliente Steam)";
    }
};

bool in_packageinfo(const store::Promo& p, const steam_local::Owned& owned) {
    return owned.packages.count(p.subid) || owned.apps.count(p.appid);
}

// Já na conta ativa (pelo pacote da promoção ou outro, ex.: comprado antes). Só dá para afirmar
// com uma única conta no PC.
bool already_owned(const store::Promo& p, const Local& local) {
    return local.root && !local.shared() && in_packageinfo(p, local.owned);
}

// A Steam recusa DLC sem o jogo base. Sem cliente Steam não há como saber; com várias contas,
// basta alguma conta do PC ter o jogo base.
bool can_claim_dlc(const store::Promo& p, const Local& local) {
    return local.root && p.parent_appid && local.owned.apps.count(p.parent_appid);
}

// Fila de resgates: abre uma página por vez na Steam e só passa para a próxima depois de confirmar
// que a licença entrou numa conta (ou o tempo esgotar). Assim dois cliques seguidos em "Resgatar"
// não se atropelam, e só entra no banco como resgatado o que entrou de fato.
//
// Confirmação: o pacote está no packageinfo.vdf E o licensecache de alguma conta mudou depois que
// a página abriu (essa conta é quem resgatou). Com uma conta só, basta o packageinfo.vdf mudar e
// conter o pacote. Se o tempo esgotar, uma notificação pergunta se o jogo já está na conta.
class ClaimQueue {
public:
    ClaimQueue(seen_db::Db& db, std::mutex& db_mu, const Local& local)
        : db_(db), db_mu_(db_mu), local_(local), thread_([this] { work(); }) {}

    void push(const store::Promo& p) {
        std::lock_guard lock(mu_);
        queue_.push_back(p);
        cv_.notify_all();
    }

    // Sem mais cliques a caminho: espera a fila esvaziar.
    void finish() {
        {
            std::lock_guard lock(mu_);
            closed_ = true;
            cv_.notify_all();
        }
        thread_.join();
    }

private:
    static constexpr auto kConfirmTimeout = std::chrono::minutes(10);
    using Time = std::filesystem::file_time_type;

    void mark_claimed(const store::Promo& p, uint32_t account, const char* why) {
        std::lock_guard lock(db_mu_);
        db_.set(account, p.subid, seen_db::Status::Claimed);
        std::cout << "  " << why << "  " << p.name << " (" << local_.name_of(account) << ")\n";
    }

    Time mtime(const std::filesystem::path& f) {
        std::error_code ec;
        auto t = std::filesystem::last_write_time(f, ec);
        return ec ? Time{} : t;
    }

    std::map<uint32_t, Time> licensecache_times() {
        std::map<uint32_t, Time> out;
        for (const auto& a : local_.accounts) out[a.id] = mtime(steam_local::licensecache_path(*local_.root, a.id));
        return out;
    }

    // Conta que recebeu a licença de `p` desde `since`, se já recebeu.
    std::optional<uint32_t> who_claimed(const store::Promo& p, const std::map<uint32_t, Time>& since,
                                        bool packageinfo_changed) {
        if (!in_packageinfo(p, steam_local::read_owned(steam_local::packageinfo_path(*local_.root))))
            return std::nullopt;
        auto now = licensecache_times();
        if (auto active = since.find(local_.account_id()); active != since.end() && now[active->first] != active->second)
            return active->first; // a conta ativa primeiro
        for (const auto& [id, t] : since)
            if (now[id] != t) return id;
        if (!local_.shared() && packageinfo_changed) return local_.account_id();
        return std::nullopt;
    }

    void work() {
        for (;;) {
            store::Promo p;
            {
                std::unique_lock lock(mu_);
                cv_.wait(lock, [&] { return closed_ || !queue_.empty(); });
                if (queue_.empty()) return;
                p = queue_.front();
                queue_.pop_front();
            }

            if (!local_.root) { // sem cliente Steam local: não há como confirmar
                notify::open_store(p.appid);
                mark_claimed(p, 0, "aberto     ");
                continue;
            }
            if (already_owned(p, local_)) {
                mark_claimed(p, local_.account_id(), "já na conta");
                continue;
            }

            const auto packageinfo = steam_local::packageinfo_path(*local_.root);
            const auto since = licensecache_times();
            auto last_packageinfo = mtime(packageinfo);
            auto last_licenses = since;
            notify::open_store(p.appid);
            std::cout << "  aguardando \"Adicionar à conta\" em " << p.name << "...\n";

            const auto deadline = std::chrono::steady_clock::now() + kConfirmTimeout;
            std::optional<uint32_t> account;
            while (!account && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                auto now_packageinfo = mtime(packageinfo);
                auto now_licenses = licensecache_times();
                bool changed = now_packageinfo != last_packageinfo;
                // Nada mudou: não relê o packageinfo.vdf (em segundo plano, cada leitura conta).
                if (!changed && now_licenses == last_licenses) continue;
                last_packageinfo = now_packageinfo;
                last_licenses = now_licenses;
                account = who_claimed(p, since, changed);
            }
            if (account) {
                mark_claimed(p, *account, "confirmado ");
                continue;
            }
            // Ex.: o jogo já estava na conta (com várias contas no PC não dá para saber antes) e a
            // Steam não registrou licença nova. Quem sabe é você.
            std::cout << "  sem confirmação em 10 min: " << p.name << ", perguntando na notificação\n";
            std::string body = p.name + "\nNão detectei a licença nova na Steam. Se já está na sua conta, marque "
                                        "para não avisar de novo.";
            if (!local_.account_name().empty()) body += "\nConta: " + local_.account_name();
            if (notify::confirm("O resgate deu certo?", body, "Já está na conta", "Avisar depois"))
                mark_claimed(p, local_.account_id(), "marcado    ");
            else std::cout << "  depois     " << p.name << " (avisa de novo na próxima)\n";
        }
    }

    seen_db::Db& db_;
    std::mutex& db_mu_;
    const Local& local_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<store::Promo> queue_;
    bool closed_ = false;
    std::thread thread_; // por último: começa a rodar depois que o resto foi inicializado
};

// Uma verificação por vez: o login, a repetição agendada e uma execução manual podem coincidir,
// e duas instâncias mostrariam as mesmas notificações em dobro. A trava some quando o processo sai.
bool single_instance() {
#ifdef _WIN32
    CreateMutexW(nullptr, FALSE, L"Local\\gamecatcher.run"); // handle fica aberto até o fim
    return GetLastError() != ERROR_ALREADY_EXISTS;
#else
    auto lock = seen_db::Db::default_path().parent_path() / "run.lock";
    std::filesystem::create_directories(lock.parent_path());
    // O_CLOEXEC: o xdg-open pode iniciar a Steam como filho, e ela não pode herdar a trava.
    int fd = open(lock.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0; // fd fica aberto até o fim
#endif
}

// Em paralelo com as promoções: se houver versão nova, pergunta numa notificação e, se você
// aceitar, troca o programa instalado (vale a partir da próxima verificação). No Windows só abre a
// página da release (ver update.hpp).
void offer_update() {
    try {
        auto r = update::check();
        if (!r) return;
        std::cout << "versão nova disponível: " << r->version << "\n";
#ifdef _WIN32
        if (notify::confirm("gamecatcher " + r->version + " disponível",
                            "Você está na " GAMECATCHER_VERSION ". Baixe o pacote novo e rode o install.bat de "
                            "novo, sem desinstalar antes.",
                            "Baixar", "Agora não"))
            notify::open_link(r->page_url);
        else
            std::cout << "  atualização adiada (avisa de novo amanhã)\n";
#else
        if (!notify::confirm("gamecatcher " + r->version + " disponível",
                             "Você está na " GAMECATCHER_VERSION ". A versão nova vem das releases do GitHub, e a "
                             "assinatura é conferida antes de instalar.",
                             "Atualizar", "Agora não")) {
            std::cout << "  atualização adiada (pergunta de novo amanhã)\n";
            return;
        }
        update::apply(*r);
        std::cout << "  atualizado para " << r->version << " (vale a partir da próxima verificação)\n";
#endif
    } catch (const std::exception& e) {
        std::cerr << "atualização: " << e.what() << "\n";
    }
}

int cmd_run(const Options& opt) {
    if (!single_instance()) {
        std::cout << "outra verificação já está em andamento\n";
        return 0;
    }
    // Só o programa instalado (o que o agendamento chama) se atualiza. O jthread espera a
    // pergunta da atualização terminar antes de sair.
    std::jthread updater;
    if (install::running_installed()) {
        install::cleanup();
        try {
            if (install::refresh_if_updated()) std::cout << "agendamento refeito para a versão " GAMECATCHER_VERSION "\n";
        } catch (const std::exception& e) {
            std::cerr << "agendamento: " << e.what() << "\n";
        }
        if (update::due()) updater = std::jthread(offer_update);
    }
    seen_db::Db db(seen_db::Db::default_path());
    std::mutex db_mu;
    const Local local = Local::load();
    const uint32_t account = local.account_id();

    std::vector<store::Promo> fresh;
    for (auto& p : find_promos_retrying(opt)) {
        if (db.get(account, p.subid)) continue;
        if (already_owned(p, local)) { // resgatado fora do gamecatcher ou comprado antes
            db.set(account, p.subid, seen_db::Status::Claimed);
            continue;
        }
        // DLC sem o jogo base: não grava no banco. Se você comprar o jogo enquanto a promoção
        // durar, a DLC passa a ser avisada.
        if (p.is_dlc && !can_claim_dlc(p, local)) continue;
        fresh.push_back(std::move(p));
    }
    if (fresh.size() > opt.max) fresh.resize(opt.max); // o resto aparece na próxima execução

    if (fresh.empty()) {
        std::cout << "nenhuma promoção nova\n";
        return 0;
    }
    std::cout << fresh.size() << " promoção(ões) nova(s) para " << local.name_of(account)
              << ", aguardando resposta nas notificações...\n";
    if (!local.root) std::cout << "(cliente Steam não encontrado: resgates não serão confirmados)\n";

    ClaimQueue claims(db, db_mu, local);
    notify::ask(fresh, local.account_name(), [&](const store::Promo& p, notify::Choice c) {
        switch (c) {
        case notify::Choice::Claim:
            std::cout << "  na fila    " << p.name << "\n";
            claims.push(p);
            break;
        case notify::Choice::Ignore: {
            std::lock_guard lock(db_mu);
            db.set(account, p.subid, seen_db::Status::Ignored);
            std::cout << "  ignorado   " << p.name << "\n";
            break;
        }
        case notify::Choice::Dismissed:
            std::cout << "  fechado    " << p.name << " (avisa de novo na próxima)\n";
            break;
        }
    });
    claims.finish();
    return 0;
}

int cmd_list(const Options& opt) {
    seen_db::Db db(seen_db::Db::default_path());
    const Local local = Local::load();
    std::cout << "conta: " << local.name_of(local.account_id())
              << (local.shared() ? " (várias contas neste PC: \"na conta\" não é verificado)" : "") << "\n";
    for (const auto& p : find_promos(opt)) {
        const char* status = already_owned(p, local)                 ? "na conta "
                             : p.is_dlc && !can_claim_dlc(p, local) ? "sem base "
                                                                     : status_label(db.get(local.account_id(), p.subid));
        std::cout << status << "  " << p.name << (p.is_dlc ? " [DLC]" : "") << "  (app " << p.appid << ", sub "
                  << p.subid << ")\n";
    }
    return 0;
}

int cmd_update() {
    if (!std::filesystem::exists(install::installed_exe()))
        throw std::runtime_error("o gamecatcher não está instalado (rode: gamecatcher install)");
    auto r = update::check();
    if (!r) {
        std::cout << "você já tem a versão mais nova (" GAMECATCHER_VERSION ")\n";
        return 0;
    }
#ifdef _WIN32
    std::cout << "versão nova: " << r->version << "\n"
              << "baixe o pacote em " << r->page_url << " e rode o install.bat de novo\n";
    notify::open_link(r->page_url);
#else
    std::cout << "baixando " << r->asset << "...\n";
    update::apply(*r);
    std::cout << "atualizado para " << r->version << "\n";
#endif
    return 0;
}

int cmd_db(bool clear) {
    auto path = seen_db::Db::default_path();
    seen_db::Db db(path);
    if (clear) {
        db.clear();
        std::cout << "banco apagado\n";
        return 0;
    }
    const Local local = Local::load();
    size_t total = 0;
    for (const auto& [account, subs] : db.entries()) {
        std::cout << local.name_of(account) << ":\n";
        for (const auto& [subid, status] : subs) std::cout << "  " << status_label(status) << "  sub " << subid << "\n";
        total += subs.size();
    }
    std::cout << total << " entradas em " << path.string() << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::cout.setf(std::ios::unitbuf); // saída aparece na hora (logs, terminal)
    try {
        int i = 1;
        std::string cmd = "run";
        if (argc > 1 && argv[1][0] != '-') cmd = argv[i++];

        if (cmd == "db") return cmd_db(i < argc && std::string(argv[i]) == "clear");
        if (cmd == "install") {
            install::install(install::self_exe());
            return 0;
        }
        if (cmd == "uninstall") {
            install::uninstall();
            return 0;
        }
        if (cmd == "update") return cmd_update();

        Options opt;
        for (; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--games-only") opt.games_only = true;
            else if (a == "--cc" && i + 1 < argc) opt.country = argv[++i];
            else if (a == "--max" && i + 1 < argc) opt.max = std::stoul(argv[++i]);
            else if (a == "-h" || a == "--help") { std::cout << kUsage; return 0; }
            else if (a == "-V" || a == "--version") { std::cout << "gamecatcher " GAMECATCHER_VERSION "\n"; return 0; }
            else throw std::runtime_error("opção desconhecida: " + a);
        }
        if (cmd == "run") return cmd_run(opt);
        if (cmd == "list") return cmd_list(opt);
        std::cout << kUsage;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "erro: " << e.what() << "\n";
        return 1;
    }
}
