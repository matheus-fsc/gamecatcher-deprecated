#include "notify.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

extern char** environ;

namespace notify {
namespace {

// Roda um programa (sem shell, então nomes de jogos não precisam de escape) e devolve
// {exit code, stdout}. Sem `capture`, o stdout vai para /dev/null: o xdg-open pode iniciar a
// Steam como processo filho, e ela herdaria o pipe, travando o read() até a Steam fechar.
std::pair<int, std::string> run(const std::vector<std::string>& args, bool capture = true) {
    // O_CLOEXEC: com várias notificações em paralelo, sem isso o pipe de uma thread vazaria para
    // o notify-send de outra e o read() só veria EOF quando aquela outra notificação fechasse.
    int out[2];
    if (pipe2(out, O_CLOEXEC) != 0) throw std::runtime_error("pipe falhou");

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (capture) posix_spawn_file_actions_adddup2(&fa, out[1], STDOUT_FILENO);
    else posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&fa, out[0]);

    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(out[1]);
    if (rc != 0) {
        close(out[0]);
        throw std::runtime_error(args[0] + " não encontrado");
    }

    std::string output;
    char buf[256];
    for (ssize_t n; (n = read(out[0], buf, sizeof buf)) > 0;) output.append(buf, static_cast<size_t>(n));
    close(out[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
}

// Há quanto tempo o cliente Steam está rodando, ou nullopt se não está.
std::optional<std::chrono::seconds> steam_age() {
    const char* home = std::getenv("HOME");
    if (!home) return std::nullopt;
    std::ifstream pidfile(std::string(home) + "/.steam/steam.pid");
    long pid = 0;
    if (!(pidfile >> pid) || pid <= 0) return std::nullopt;

    // /proc/<pid>/stat: o campo 22 (starttime, em ticks desde o boot) vem depois do ")" do nome.
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(stat, line)) return std::nullopt; // processo não existe mais
    std::istringstream fields(line.substr(line.rfind(')') + 2));
    std::string field;
    for (int i = 3; i < 22 && fields >> field;) ++i;
    unsigned long long start_ticks = 0;
    if (!(fields >> start_ticks)) return std::nullopt;

    double uptime = 0;
    std::ifstream("/proc/uptime") >> uptime;
    double age = uptime - static_cast<double>(start_ticks) / static_cast<double>(sysconf(_SC_CLK_TCK));
    return std::chrono::seconds(static_cast<long long>(std::max(age, 0.0)));
}

Choice ask_one(const store::Promo& p, const std::string& account) {
    // --action implica --wait: notify-send só sai quando o usuário escolhe ou fecha,
    // e imprime o nome da ação escolhida. -t 0: não expira sozinha.
    auto [code, out] = run({
        "notify-send",
        "--app-name=gamecatcher",
        "--icon=steam",
        "--expire-time=0",
        "--action=claim=Resgatar",
        "--action=ignore=Ignorar",
        detail::title(p),
        detail::body(p, account),
    });
    if (out.starts_with("claim")) return Choice::Claim;
    if (out.starts_with("ignore")) return Choice::Ignore;
    return Choice::Dismissed;
}

} // namespace

void ask(const std::vector<store::Promo>& promos, const std::string& account,
         const std::function<void(const store::Promo&, Choice)>& on_choice) {
    std::mutex mu;
    std::vector<std::thread> threads;
    for (const auto& p : promos) {
        threads.emplace_back([&, &p = p] {
            Choice c = ask_one(p, account);
            std::lock_guard lock(mu);
            on_choice(p, c);
        });
    }
    for (auto& t : threads) t.join();
}

void open_store(uint32_t appid) {
    // A Steam recém-aberta (ainda fazendo login) descarta links steam:// que chegam nesse
    // intervalo. Então: aberturas em fila; se a Steam é jovem, espera ela completar kWarmup.
    constexpr auto kWarmup = std::chrono::seconds(40);
    static std::mutex mu;
    static std::optional<std::chrono::steady_clock::time_point> launched_by_us;
    std::lock_guard lock(mu);

    auto age = steam_age();
    if (launched_by_us) {
        auto ours = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - *launched_by_us);
        age = age ? std::min(*age, ours) : ours; // o steam.pid pode ainda não ter sido atualizado
    }
    if (age && *age < kWarmup) std::this_thread::sleep_for(kWarmup - *age);
    if (!age) launched_by_us = std::chrono::steady_clock::now(); // este clique vai abrir a Steam

    // xdg-open sai com erro se não houver handler para steam:// (cliente não instalado).
    if (run({"xdg-open", "steam://store/" + std::to_string(appid)}, false).first != 0)
        run({"xdg-open", "https://store.steampowered.com/app/" + std::to_string(appid) + "/"}, false);
}

} // namespace notify
