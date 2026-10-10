#pragma once

// Instalação para o usuário atual, sem administrador.
//   Linux:   `gamecatcher install` copia para ~/.local/bin e agenda: autostart XDG no login e
//            timer do systemd do usuário (6 h). Rodar de novo atualiza a instalação.
//   Windows: o install.bat do pacote (PowerShell) copia para %LOCALAPPDATA%\gamecatcher\bin e cria
//            a tarefa. O programa só troca o próprio executável nas atualizações (ver install.cpp).

#include <filesystem>
#include <string>

namespace install {

std::filesystem::path installed_exe(); // onde o programa fica depois de instalado
std::filesystem::path self_exe();      // o executável deste processo
bool running_installed();              // este processo é o programa instalado

// Linux: copia `from` para installed_exe() (se já não for ele) e (re)cria o agendamento.
// Windows: lança, indicando o install.bat.
void install(const std::filesystem::path& from);
// Linux: remove o agendamento e o programa; o seen.db fica. Windows: lança, indicando o uninstall.bat.
void uninstall();

// Troca o executável instalado por `data`, mesmo com ele rodando. No Windows o antigo fica como
// ".old" até a próxima execução (cleanup()), porque um .exe em uso não pode ser apagado.
void replace_installed(const std::string& data);
void cleanup();

// Linux: depois de uma atualização, o programa novo refaz o agendamento na primeira execução (pode
// ter mudado entre versões). true se refez. Windows: nada (a tarefa é do install.bat).
bool refresh_if_updated();

} // namespace install
