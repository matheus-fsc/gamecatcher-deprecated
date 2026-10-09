#pragma once

#include <map>
#include <string>

// Autenticação Steam via IAuthenticationService (portado de DoctorMcKay/node-steam-session).
//
// Usa a plataforma MobileApp: desde 2025-04 é a única em que GenerateAccessTokenForApp
// funciona fora de uma conexão CM, e o access token dela serve direto como cookie
// steamLoginSecure (sem finalizelogin/settoken).

namespace auth {

struct Credentials {
    std::string steamid;       // SteamID64
    std::string account_name;
    std::string refresh_token; // JWT, ~200 dias
};

struct WebCookies {
    std::string steam_login_secure;
    std::string sessionid;
    // Cookies que a loja seta na primeira visita (steamCountry, browserid...). Sem eles a loja
    // responde 302 para a mesma URL, e um POST redirecionado vira GET.
    std::map<std::string, std::string> extra;

    std::string header() const {
        std::string h = "steamLoginSecure=" + steam_login_secure + "; sessionid=" + sessionid;
        for (const auto& [k, v] : extra) h += "; " + k + "=" + v;
        return h;
    }
};

// Login interativo: mostra um QR no terminal e espera a aprovação no app Steam.
Credentials login_qr();

// Gera cookies novos a partir do refresh token. Se a Steam emitir um refresh token
// novo (renovação), `creds` é atualizado e a função retorna true; persista-o.
bool web_cookies(Credentials& creds, WebCookies& out);

} // namespace auth
