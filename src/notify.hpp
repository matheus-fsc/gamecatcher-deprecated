#pragma once

// Notificação na área de trabalho com dois botões: "Resgatar" e "Ignorar".
//   Linux: notify-send (libnotify >= 0.7.12) como subprocesso.
//   Windows: toast nativo via C++/WinRT.

#include "store.hpp"

#include <functional>
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

namespace detail {
// Textos comuns às duas plataformas.
std::string title(const store::Promo& p);
std::string body(const store::Promo& p, const std::string& account);
} // namespace detail

// Abre a página do app no cliente Steam (steam://store/<appid>), ou no navegador se não houver cliente.
void open_store(uint32_t appid);

} // namespace notify
