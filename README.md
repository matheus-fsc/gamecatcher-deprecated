# gamecatcher

> [!WARNING]
> **ATENÇÃO: este script NÃO é funcional e VAI causar o bloqueio da sua conta Steam.**
>
> Na primeira vez que foi usado, a Steam bloqueou a conta do autor como "possivelmente acessada por outra pessoa":
> compras, trocas, presentes, ativação de chaves e a Comunidade ficaram desativadas até a recuperação pelo Suporte.
>
> Além disso, automatizar o acesso à Steam viola o [Acordo de Assinatura Steam](https://store.steampowered.com/subscriber_agreement/)
> (seção 4.C proíbe "scripts, bots, macros ou outros sistemas não controlados por humanos"), e a Valve pode restringir
> ou encerrar a conta sem aviso (seção 4.D).
>
> **Não rode isto com a sua conta.** O código fica publicado só como registro de estudo.

## O que era

Uma CLI em C++ (Linux/Windows) para listar e resgatar jogos e DLCs pagos que estavam grátis por tempo limitado
(promoções de 100% "grátis para manter") na loja Steam.

## Por que a conta foi bloqueada

O login (`gamecatcher login`) se apresentava à Steam como **o app mobile Android** (um "Galaxy S25" que não existe).
Ele imitava o que a biblioteca [`steam-session`](https://github.com/DoctorMcKay/node-steam-session) faz, para poder
renovar a sessão sozinho. Mesmo com o login aprovado no próprio Steam Guard do dono, o sistema antifraude da Steam
viu um celular desconhecido entrar na conta e a restringiu como se ela tivesse sido roubada.

## Como funcionava

1. `login`: login por QR code via `IAuthenticationService` (`BeginAuthSessionViaQR` → `PollAuthSessionStatus`),
   com protobuf codificado à mão; salvava o refresh token.
2. `claim`: gerava o cookie `steamLoginSecure` com `GenerateAccessTokenForApp`, buscava as promoções em
   `/search/results/?maxprice=free&specials=1`, consultava os pacotes "grátis para manter" em
   `IStoreBrowseService/GetItems` e fazia `POST /freelicense/addfreelicense/` para cada subid.

```
src/
  auth.*         login por QR e geração de cookies
  protobuf.hpp   protobuf mínimo (varint, fixed64, length-delimited)
  http.*         wrapper de libcurl
  store.*        busca, GetItems e resgate
  token_store.*  armazenamento do refresh token
  main.cpp       CLI
```

Dependências: libcurl, [nlohmann/json](https://github.com/nlohmann/json) e
[nayuki/QR-Code-generator](https://github.com/nayuki/QR-Code-generator) (essas duas são baixadas pelo CMake).

## Créditos

- Fluxo de autenticação portado de [DoctorMcKay/node-steam-session](https://github.com/DoctorMcKay/node-steam-session).
- Definições protobuf de [SteamDatabase/Protobufs](https://github.com/SteamDatabase/Protobufs).
