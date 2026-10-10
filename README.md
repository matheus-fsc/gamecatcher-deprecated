# gamecatcher

[English](README.en.md) | Português

Avisa na área de trabalho quando um jogo ou DLC pago fica grátis para manter na Steam (promoção de 100%). Cada aviso tem dois botões: **Resgatar** abre a página na Steam para você clicar em "Adicionar à conta", e **Ignorar** faz o aviso não voltar mais.

O gamecatcher **não faz login na Steam** e não mexe na sua conta. Ele só consulta a loja pública e lê, sem alterar, alguns arquivos do cliente Steam instalado no seu PC. O resgate é sempre feito por você, dentro da Steam.

Funciona no Linux (KDE, GNOME e outros ambientes com notificações) e no Windows 10/11.

## Como funciona

1. Busca as promoções na loja pública da Steam (sem login).
2. Ignora o que você já tem, o que você já respondeu antes e DLCs cujo jogo base você não tem.
3. Mostra uma notificação para cada promoção nova (até 5 por vez).
4. Ao clicar em **Resgatar**, abre a página no cliente Steam. Se você clicar em vários, eles entram numa fila: a próxima página só abre depois que a anterior foi adicionada à conta. Se o gamecatcher não detectar o resgate em 10 minutos (por exemplo, porque o jogo já estava na conta), uma notificação pergunta se ele já está na conta: **Já está na conta** marca como resgatado; **Avisar depois** avisa de novo na próxima verificação.
5. Guarda num banco local o que foi resgatado ou ignorado, separado por conta Steam.

A verificação roda a cada login no computador e, para quem deixa o PC ligado, a cada 6 horas. Entre uma verificação e outra o programa não fica rodando. Se uma verificação ainda está esperando resposta nas notificações, a seguinte não começa.

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
tar xzf gamecatcher-<versão>-linux-x86_64.tar.gz
cd gamecatcher-<versão>-linux-x86_64
./gamecatcher install
```

O programa se copia para `~/.local/bin/gamecatcher` e cria `~/.config/autostart/gamecatcher.desktop`, que roda uma verificação a cada login, e um timer do systemd do usuário (`~/.config/systemd/user/gamecatcher.timer`) que roda a cada 6 horas. Não precisa de root. `./packaging/linux/install.sh` faz a mesma coisa.

Para remover: `gamecatcher uninstall`

### Windows

Requisito: Windows 10 21H2 ou mais novo, ou Windows 11.

1. Extraia `gamecatcher-<versão>-windows-x64.zip`.
2. Na pasta extraída, dê **dois cliques em `install.bat`**.

Pronto. O programa é copiado para `%LOCALAPPDATA%\gamecatcher\bin` e a tarefa `gamecatcher` é criada no Agendador de Tarefas: uma verificação 1 minuto depois de cada logon e a cada 6 horas. Não precisa de administrador.

Para remover: dois cliques em `uninstall.bat`.

<details>
<summary>Sem o .bat, pelo PowerShell</summary>

Na raiz da pasta extraída (onde está o `gamecatcher.exe`):

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1
powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -Uninstall
```

`.\install.ps1` direto dá erro de "não está assinado digitalmente": o Windows bloqueia scripts baixados da internet. O `-ExecutionPolicy Bypass` libera só esta execução. O `install.bat` já faz isso por você.
</details>

**Por que um .bat e não o próprio `gamecatcher.exe`?** No Windows, quem cria a tarefa agendada é o PowerShell (assinado pela Microsoft). Um `.exe` sem assinatura digital que cria a própria tarefa de logon tem o comportamento típico de malware ("persistência"), e o Windows Defender bloqueia (`Behavior:Win32/Persistence.A!ml`). Por isso o gamecatcher não faz isso sozinho no Windows.

## Atualizações

O programa instalado procura uma versão nova uma vez por dia, nas [releases do GitHub](https://github.com/matheus-fsc/gamecatcher/releases) (API pública, sem login). Se houver, mostra uma notificação:

- **Atualizar**: baixa a versão nova e troca o programa. Vale a partir da próxima verificação.
- **Agora não**: pergunta de novo no dia seguinte.

Nada é instalado sem o seu clique. Para atualizar na hora: `gamecatcher update` (no Windows: `%LOCALAPPDATA%\gamecatcher\bin\gamecatcher.exe update`).

Cada release publica o binário de cada sistema com uma **assinatura Ed25519**. A chave pública está embutida no programa e a privada só existe como segredo no CI do GitHub. Antes de trocar qualquer arquivo, o atualizador confere a assinatura: um download corrompido ou uma release adulterada é recusada e nada muda.

Atualizar pelo pacote também funciona: extraia a versão nova e rode o instalador de novo (`install.bat` ou `gamecatcher install`), sem desinstalar antes.

Quem tem a 1.0.x precisa instalar a 1.1.0 uma vez pelo pacote. Daí em diante as atualizações chegam sozinhas.

## Uso manual

```
gamecatcher                  verifica agora e mostra as notificações
gamecatcher list             lista as promoções e o status de cada uma
gamecatcher db               mostra o banco local
gamecatcher db clear         apaga o banco local (tudo volta a ser avisado)
gamecatcher update           baixa e instala a versão mais nova (assinatura conferida)
gamecatcher install          (Linux) instala e agenda as verificações
gamecatcher uninstall        (Linux) remove o programa e o agendamento
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
