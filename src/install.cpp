#include "install.hpp"

#include "seen_db.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace install {
namespace {

bool same_file(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

#ifndef _WIN32

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("não foi possível ler " + p.string());
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& p, const std::string& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f.write(data.data(), static_cast<std::streamsize>(data.size())) || !f.flush())
        throw std::runtime_error("não foi possível gravar " + p.string());
}

fs::path state_dir() { return seen_db::Db::default_path().parent_path(); }
fs::path version_file() { return state_dir() / "installed-version"; }

void save_version() {
    fs::create_directories(state_dir());
    write_file(version_file(), GAMECATCHER_VERSION);
}

// Roda um programa sem shell, com a saída descartada, e devolve o exit code.
int run(const std::vector<std::string>& args) {
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
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

fs::path home() {
    const char* h = std::getenv("HOME");
    if (!h) throw std::runtime_error("HOME não definido");
    return h;
}

fs::path config_dir() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    return xdg && *xdg ? fs::path(xdg) : home() / ".config";
}

fs::path autostart_file() { return config_dir() / "autostart" / "gamecatcher.desktop"; }
fs::path units_dir() { return config_dir() / "systemd" / "user"; }
bool has_systemd() { return run({"systemctl", "--user", "show-environment"}) == 0; }

void schedule(const fs::path& exe) {
    const std::string quoted = "\"" + exe.string() + "\"";
    fs::create_directories(autostart_file().parent_path());
    write_file(autostart_file(), "[Desktop Entry]\n"
                                 "Type=Application\n"
                                 "Name=gamecatcher\n"
                                 "Comment=Avisa quando um jogo pago fica grátis na Steam\n"
                                 "Exec=" + quoted + " run\n"
                                 "Icon=steam\n"
                                 "Terminal=false\n"
                                 "NoDisplay=true\n"
                                 "X-GNOME-Autostart-enabled=true\n"
                                 "X-KDE-autostart-phase=2\n");
    std::cout << "verificação a cada login: " << autostart_file().string() << "\n";

    if (!has_systemd()) {
        std::cout << "systemd do usuário indisponível: só a verificação do login\n";
        return;
    }
    fs::create_directories(units_dir());
    write_file(units_dir() / "gamecatcher.service", "[Unit]\n"
                                                    "Description=gamecatcher: verifica promoções grátis na Steam\n\n"
                                                    "[Service]\n"
                                                    "# Fica rodando até as notificações serem respondidas.\n"
                                                    "Type=exec\n"
                                                    "ExecStart=" + quoted + " run\n");
    write_file(units_dir() / "gamecatcher.timer",
               "[Unit]\n"
               "Description=gamecatcher a cada 6 horas (o login já roda pelo autostart)\n\n"
               "[Timer]\n"
               "# Contado a partir do login: a primeira repetição é 6 h depois, sem coincidir com o autostart.\n"
               "OnActiveSec=6h\n"
               "OnUnitActiveSec=6h\n\n"
               "[Install]\n"
               "WantedBy=timers.target\n");
    run({"systemctl", "--user", "daemon-reload"});
    if (run({"systemctl", "--user", "enable", "--now", "gamecatcher.timer"}) != 0)
        throw std::runtime_error("systemctl --user enable gamecatcher.timer falhou");
    std::cout << "e a cada 6 horas: systemctl --user list-timers gamecatcher.timer\n";
}

void unschedule() {
    std::error_code ec;
    fs::remove(autostart_file(), ec);
    if (!has_systemd()) return;
    run({"systemctl", "--user", "disable", "--now", "gamecatcher.timer"});
    fs::remove(units_dir() / "gamecatcher.service", ec);
    fs::remove(units_dir() / "gamecatcher.timer", ec);
    run({"systemctl", "--user", "daemon-reload"});
}

#endif

} // namespace

fs::path installed_exe() {
#ifdef _WIN32
    const char* local = std::getenv("LOCALAPPDATA");
    if (!local) throw std::runtime_error("LOCALAPPDATA não definido");
    return fs::path(local) / "gamecatcher" / "bin" / "gamecatcher.exe";
#else
    return home() / ".local" / "bin" / "gamecatcher";
#endif
}

fs::path self_exe() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size()) return fs::path(buf.substr(0, n));
        buf.resize(buf.size() * 2);
    }
#else
    return fs::read_symlink("/proc/self/exe");
#endif
}

bool running_installed() { return same_file(self_exe(), installed_exe()); }

#ifndef _WIN32
void replace_installed(const std::string& data) {
    const fs::path exe = installed_exe();
    fs::path fresh = exe;
    fresh += ".new";
    fs::create_directories(exe.parent_path());
    write_file(fresh, data);
    fs::permissions(fresh, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                               fs::perms::others_read | fs::perms::others_exec);
    fs::rename(fresh, exe); // atômico; quem já está rodando continua com o arquivo antigo
}
#endif

void cleanup() {
    std::error_code ec;
    fs::path old = installed_exe();
    old += ".old";
    fs::remove(old, ec); // falha enquanto a versão antiga ainda estiver rodando: tenta na próxima
}

// No Windows, quem cria a tarefa é o install.bat (PowerShell, assinado pela Microsoft). Um .exe
// sem assinatura que cria a própria tarefa de logon é bloqueado pelo Defender
// (Behavior:Win32/Persistence.A!ml), então o programa não faz isso.

void install([[maybe_unused]] const fs::path& from) {
#ifdef _WIN32
    throw std::runtime_error("no Windows, instale com o install.bat (dois cliques) da pasta do pacote");
#else
    const fs::path exe = installed_exe();
    if (!same_file(from, exe)) replace_installed(read_file(from));
    schedule(exe);
    save_version();
    std::cout << "instalado em " << exe.string() << "\n";
#endif
}

void uninstall() {
#ifdef _WIN32
    throw std::runtime_error("no Windows, desinstale com o uninstall.bat da pasta do pacote");
#else
    unschedule();
    std::error_code ec;
    fs::remove(version_file(), ec);
    // ~/.local/bin é compartilhado com outros programas: só o nosso arquivo.
    fs::remove(installed_exe(), ec);
    std::cout << "removido (o banco seen.db em " << state_dir().string() << " foi mantido)\n";
#endif
}

bool refresh_if_updated() {
#ifdef _WIN32
    return false; // a tarefa é do install.bat; se uma versão mudar o agendamento, as notas avisam
#else
    if (!running_installed()) return false;
    std::error_code ec;
    if (fs::exists(version_file(), ec) && read_file(version_file()) == GAMECATCHER_VERSION) return false;
    schedule(installed_exe());
    save_version();
    return true;
#endif
}

} // namespace install
