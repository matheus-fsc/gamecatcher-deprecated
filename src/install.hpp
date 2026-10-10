#pragma once

// Instalação para o usuário atual, sem administrador.
//   Linux:   `gamecatcher install` copia para ~/.local/bin e agenda: autostart XDG no login e
//            timer do systemd do usuário (6 h). Rodar de novo atualiza a instalação.
//   Windows: o install.bat do pacote (PowerShell) copia para %LOCALAPPDATA%\gamecatcher\bin e cria
//            a tarefa. O programa nunca cria tarefas nem troca o próprio executável (Defender).

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

#ifndef _WIN32
// Troca o executável instalado por `data`, mesmo com ele rodando (quem já roda segue com o antigo).
void replace_installed(const std::string& data);
#endif
// Windows: o install.ps1 renomeia um .exe em uso para ".old"; apaga esse resto na próxima execução.
void cleanup();

// Linux: depois de uma atualização, o programa novo refaz o agendamento na primeira execução (pode
// ter mudado entre versões). true se refez. Windows: nada (a tarefa é do install.bat).
bool refresh_if_updated();

} // namespace install
