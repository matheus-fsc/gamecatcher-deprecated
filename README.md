# gamecatcher

[English](README.en.md) | Português

Avisa na área de trabalho quando um jogo ou DLC pago fica grátis para manter na Steam (promoção de 100%). Cada aviso tem dois botões: **Resgatar** abre a página na Steam para você clicar em "Adicionar à conta", e **Ignorar** faz o aviso não voltar mais.

O gamecatcher **não faz login na Steam** e não mexe na sua conta. Ele só consulta a loja pública e lê, sem alterar, alguns arquivos do cliente Steam instalado no seu PC. O resgate é sempre feito por você, dentro da Steam.

Funciona no Linux (KDE, GNOME e outros ambientes com notificações) e no Windows 10/11.

## Como funciona

1. Busca as promoções na loja pública da Steam (sem login).
2. Ignora o que você já tem, o que você já respondeu antes e DLCs cujo jogo base você não tem.
3. Mostra uma notificação para cada promoção nova (até 5 por vez).
4. Ao clicar em **Resgatar**, abre a página no cliente Steam. Se você clicar em vários, eles entram numa fila: a próxima página só abre depois que a anterior foi adicionada à conta (ou depois de 10 minutos).
5. Guarda num banco local o que foi resgatado ou ignorado, separado por conta Steam.

A verificação roda uma vez a cada login no computador.

### Como ele sabe o que você tem

Sem login, o gamecatcher usa os arquivos do cliente Steam do seu PC:

| Arquivo | Para quê |
|---|---|
| `appcache/packageinfo.vdf` | Pacotes e jogos das contas que já entraram neste cliente |
| `config/loginusers.vdf` | Contas deste cliente e qual está em uso |
| `userdata/<conta>/config/licensecache` | Só a data de modificação: muda quando aquela conta recebe uma licença nova (é assim que o resgate é confirmado) |

Sem o cliente Steam instalado, ele ainda avisa as promoções de jogos, mas não sabe o que você já tem e não confirma os resgates.

### Várias contas no mesmo PC

O `packageinfo.vdf` é compartilhado por todas as contas do cliente. Por isso, quando há mais de uma conta:

- Os avisos e o banco local são separados por conta (a conta em uso aparece na notificação).
- O gamecatcher não consegue saber se a conta em uso já tem um jogo. Ele avisa, e você responde uma vez por conta.
- Uma DLC é avisada se **alguma** conta do PC tiver o jogo base. Se a conta em uso não tiver, a Steam não deixa adicionar; clique em **Ignorar**.

## Instalação

Baixe o pacote do seu sistema na [página de releases](https://github.com/matheus-fsc/gamecatcher/releases/latest).

### Linux

Requisitos: `libcurl` e `libsystemd`, já presentes no Ubuntu, Pop!_OS, Fedora, Arch e na maioria das distribuições.

```sh
tar xzf gamecatcher-1.0.0-linux-x86_64.tar.gz
cd gamecatcher-1.0.0-linux-x86_64
./packaging/linux/install.sh
```

O instalador copia o programa para `~/.local/bin/gamecatcher` e cria `~/.config/autostart/gamecatcher.desktop`, que roda uma verificação a cada login.

Para remover: `./packaging/linux/install.sh --uninstall`

### Windows

Requisito: Windows 10 21H2 ou mais novo, ou Windows 11.

1. Extraia `gamecatcher-1.0.0-windows-x64.zip`.
2. No PowerShell, dentro da pasta extraída:

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1
```

O instalador copia o programa para `%LOCALAPPDATA%\gamecatcher\bin` e cria a tarefa `gamecatcher` no Agendador de Tarefas, que roda uma verificação 1 minuto depois de cada logon. Não precisa de administrador.

Para remover: `powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -Uninstall`

## Uso manual

```
gamecatcher                  verifica agora e mostra as notificações
gamecatcher list             lista as promoções e o status de cada uma
gamecatcher db               mostra o banco local
gamecatcher db clear         apaga o banco local (tudo volta a ser avisado)
```

Opções:

| Opção | Efeito |
|---|---|
| `--games-only` | Não avisa DLCs |
| `--cc XX` | País da loja (padrão: detectado pelo IP) |
| `--max N` | No máximo N notificações por vez (padrão 5) |
| `--version` | Mostra a versão |

O banco local fica em `~/.local/state/gamecatcher/seen.db` (Linux) ou `%LOCALAPPDATA%\gamecatcher\seen.db` (Windows).

## Compilar do código-fonte

Requisitos: CMake 3.20+, compilador C++20, libcurl. A nlohmann/json é baixada pelo CMake se não estiver instalada.

Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Windows (Visual Studio 2022 e [vcpkg](https://vcpkg.io)):

```powershell
vcpkg install curl:x64-windows-static nlohmann-json:x64-windows-static
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

## Histórico: versão 0.1 alpha (descontinuada)

A primeira versão ([v0.1-alpha](https://github.com/matheus-fsc/gamecatcher/releases/tag/v0.1-alpha)) fazia login na Steam se passando pelo app mobile e resgatava as promoções sozinha. No primeiro uso, a Steam bloqueou a conta do autor como "possivelmente acessada por outra pessoa". Além disso, automatizar o acesso à conta viola o [Acordo de Assinatura Steam](https://store.steampowered.com/subscriber_agreement/) (seções 4.C e 4.D).

**Não use a 0.1.** A versão 1.0 foi reescrita para não fazer login nem resgatar nada sozinha.
