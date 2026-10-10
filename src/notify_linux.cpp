#include "notify.hpp"

#include <fcntl.h>
#include <systemd/sd-bus.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern char** environ;

namespace notify {
namespace {

// Roda um programa sem shell e devolve o exit code. O stdout vai para /dev/null: o xdg-open pode
// iniciar a Steam como processo filho, e ela não deve herdar nada nosso.
int run(const std::vector<std::string>& args) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);

    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return -1;
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Há quanto tempo o cliente Steam está rodando, ou nullopt se não está.
std::optional<std::chrono::seconds> steam_process_age() {
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

constexpr const char* kService = "org.freedesktop.Notifications";
constexpr const char* kPath = "/org/freedesktop/Notifications";

struct BusUnref {
    void operator()(sd_bus* b) const { sd_bus_flush_close_unref(b); }
};
struct MsgUnref {
    void operator()(sd_bus_message* m) const { sd_bus_message_unref(m); }
};
using Bus = std::unique_ptr<sd_bus, BusUnref>;
using Msg = std::unique_ptr<sd_bus_message, MsgUnref>;

void check(int r, const char* what) {
    if (r < 0) throw std::runtime_error(std::string("D-Bus (") + what + "): " + std::strerror(-r));
}

// Uma notificação com botões e quem recebe a resposta.
struct Notification {
    std::string title, body;
    std::vector<std::pair<std::string, std::string>> actions; // (chave devolvida no clique, texto do botão)
    std::function<void(const std::string& key)> answer;        // "" = fechada sem escolher
};

// Estado do laço de eventos: notificações abertas esperando resposta.
struct Pending {
    sd_bus* bus = nullptr;
    std::map<uint32_t, const Notification*> by_id;

    void resolve(uint32_t id, const std::string& key) {
        auto it = by_id.find(id);
        if (it == by_id.end()) return; // de outro programa, ou já respondida
        const Notification& n = *it->second;
        by_id.erase(it);
        n.answer(key);
    }
};

int on_action(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto& st = *static_cast<Pending*>(userdata);
    uint32_t id = 0;
    const char* key = nullptr;
    if (sd_bus_message_read(m, "us", &id, &key) < 0) return 0;
    auto it = st.by_id.find(id);
    if (it == st.by_id.end()) return 0;
    std::string k = key;
    const auto& actions = it->second->actions;
    // "default": se o servidor fechar a notificação, vira "fechada".
    if (std::none_of(actions.begin(), actions.end(), [&](auto& a) { return a.first == k; })) return 0;
    // Alguns servidores mantêm a notificação depois do clique; fecha para não ficar um botão morto.
    sd_bus_call_method(st.bus, kService, kPath, kService, "CloseNotification", nullptr, nullptr, "u", id);
    st.resolve(id, k);
    return 0;
}

int on_closed(sd_bus_message* m, void* userdata, sd_bus_error*) {
    uint32_t id = 0, reason = 0;
    if (sd_bus_message_read(m, "uu", &id, &reason) >= 0) static_cast<Pending*>(userdata)->resolve(id, "");
    return 0;
}

uint32_t show(sd_bus* bus, const Notification& n) {
    sd_bus_message* raw = nullptr;
    check(sd_bus_message_new_method_call(bus, &raw, kService, kPath, kService, "Notify"), "Notify");
    Msg m(raw);
    // Notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout)
    check(sd_bus_message_append(m.get(), "susss", "gamecatcher", 0u, "steam", n.title.c_str(), n.body.c_str()), "args");
    // "default" = clique no corpo da notificação. Sem ela, alguns servidores (KDE) disparam a
    // primeira ação no clique do corpo, e um clique distraído viraria "Resgatar".
    std::vector<char*> actions{const_cast<char*>("default"), const_cast<char*>("")};
    for (const auto& [key, text] : n.actions) {
        actions.push_back(const_cast<char*>(key.c_str()));
        actions.push_back(const_cast<char*>(text.c_str()));
    }
    actions.push_back(nullptr);
    check(sd_bus_message_append_strv(m.get(), actions.data()), "actions");
    check(sd_bus_message_append(m.get(), "a{sv}", 1, "urgency", "y", static_cast<uint8_t>(1)), "hints");
    check(sd_bus_message_append(m.get(), "i", 0), "timeout"); // 0 = não expira sozinha

    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply_raw = nullptr;
    int r = sd_bus_call(bus, m.get(), 0, &err, &reply_raw);
    Msg reply(reply_raw);
    if (r < 0) {
        std::string msg = err.message ? err.message : std::strerror(-r);
        sd_bus_error_free(&err);
        throw std::runtime_error("serviço de notificações indisponível: " + msg);
    }
    uint32_t id = 0;
    check(sd_bus_message_read(reply.get(), "u", &id), "id");
    return id;
}

// Fala direto com o servidor de notificações (org.freedesktop.Notifications) via D-Bus. É a
// especificação que GNOME, KDE, XFCE, Cinnamon, MATE, COSMIC, dunst e mako implementam; não
// depende da versão do notify-send (o --action dele só existe a partir da libnotify 0.7.10, e o
// Ubuntu 22.04 vem com a 0.7.9). Cada chamada abre a própria conexão, então dá para usar de
// threads diferentes ao mesmo tempo. Retorna quando todas foram respondidas ou fechadas.
void show_and_wait(const std::vector<Notification>& ns) {
    sd_bus* raw = nullptr;
    check(sd_bus_open_user(&raw), "conectar ao barramento da sessão");
    Bus bus(raw);

    Pending st{.bus = bus.get(), .by_id = {}};
    check(sd_bus_match_signal(bus.get(), nullptr, nullptr, kPath, kService, "ActionInvoked", on_action, &st),
          "ActionInvoked");
    check(sd_bus_match_signal(bus.get(), nullptr, nullptr, kPath, kService, "NotificationClosed", on_closed, &st),
          "NotificationClosed");

    // Sinais que chegam durante o sd_bus_call ficam na fila e só são tratados no laço abaixo,
    // depois que todos os ids já estão no mapa.
    for (const auto& n : ns) st.by_id[show(bus.get(), n)] = &n;

    while (!st.by_id.empty()) {
        int r = sd_bus_process(bus.get(), nullptr);
        check(r, "processar");
        if (r > 0) continue;
        check(sd_bus_wait(bus.get(), UINT64_MAX), "esperar");
    }
}

} // namespace

void ask(const std::vector<store::Promo>& promos, const std::string& account,
         const std::function<void(const store::Promo&, Choice)>& on_choice) {
    std::vector<Notification> ns;
    for (const auto& p : promos)
        ns.push_back({detail::title(p), detail::body(p, account), {{"claim", "Resgatar"}, {"ignore", "Ignorar"}},
                      [&on_choice, &p](const std::string& k) {
                          on_choice(p, k == "claim" ? Choice::Claim : k == "ignore" ? Choice::Ignore : Choice::Dismissed);
                      }});
    show_and_wait(ns);
}

bool confirm(const std::string& title, const std::string& body, const std::string& yes, const std::string& no) {
    bool answer = false;
    show_and_wait({{title, body, {{"yes", yes}, {"no", no}}, [&](const std::string& k) { answer = k == "yes"; }}});
    return answer;
}

namespace detail {

std::optional<std::chrono::seconds> steam_age() { return steam_process_age(); }

// xdg-open sai com erro se não houver handler para o esquema (ex.: steam:// sem cliente).
bool open_url(const std::string& url) { return run({"xdg-open", url}) == 0; }

} // namespace detail

} // namespace notify
