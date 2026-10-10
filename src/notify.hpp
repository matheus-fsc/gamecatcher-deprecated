#pragma once

// Notificação na área de trabalho com dois botões: "Resgatar" e "Ignorar".
//   Linux: D-Bus (org.freedesktop.Notifications) via sd-bus.
//   Windows: toast nativo via C++/WinRT.

#include "store.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace notify {

enum class Choice { Claim, Ignore, Dismissed };

// Mostra uma notificação por promoção, todas ao mesmo tempo, e chama `on_choice` (de qualquer
// thread, uma por vez) conforme o usuário responde. Retorna quando todas foram respondidas ou
// fechadas. "Dismissed" = fechada sem escolher (volta a aparecer na próxima execução).
// `account`: nome da conta Steam ativa, mostrado na notificação (vazio = não mostra).
void ask(const std::vector<store::Promo>& promos, const std::string& account,
         const std::function<void(const store::Promo&, Choice)>& on_choice);

// Pergunta de sim/não numa notificação (ex.: "o resgate deu certo?", "atualizar?"). Bloqueia até a
// resposta; pode ser chamada de qualquer thread, ao mesmo tempo que `ask`. true = botão `yes`;
// false = botão `no` ou fechada.
bool confirm(const std::string& title, const std::string& body, const std::string& yes, const std::string& no);

namespace detail {
// Textos comuns às duas plataformas.
std::string title(const store::Promo& p);
std::string body(const store::Promo& p, const std::string& account);

// Implementados por plataforma (notify_linux.cpp / notify_windows.cpp):
// há quanto tempo o cliente Steam está rodando (nullopt = não está) e abrir um link.
std::optional<std::chrono::seconds> steam_age();
bool open_url(const std::string& url); // false se não houver programa para o link
} // namespace detail

// Abre um link no programa padrão (navegador, para https://).
inline bool open_link(const std::string& url) { return detail::open_url(url); }

// Abre a página do app no cliente Steam (steam://store/<appid>), ou no navegador se não houver cliente.
void open_store(uint32_t appid);

} // namespace notify
