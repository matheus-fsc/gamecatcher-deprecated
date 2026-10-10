# Teste do gamecatcher no Windows (instruções para um agente)

Este documento é para um agente de código (ou uma pessoa) rodando numa máquina **Windows 10 21H2+ ou Windows 11** com o cliente Steam instalado. O objetivo é validar a versão Windows, que até agora só foi **compilada** no CI e nunca rodou num Windows real.

Leia o [README](../README.md) para entender o que o programa faz antes de começar.

## Regras de segurança (obrigatórias)

O gamecatcher existe porque uma versão anterior (0.1) causou o bloqueio de uma conta Steam. Por isso:

1. **Nunca faça login na Steam**, nunca digite senha ou código do Steam Guard, nunca use a API de autenticação da Steam.
2. **Nunca clique em "Adicionar à conta"** (ou qualquer botão de compra) na Steam. Quando um teste precisar disso, **peça ao usuário humano** para clicar e espere a confirmação dele.
3. **Não altere nenhum arquivo do cliente Steam** (`appcache`, `config`, `userdata`). O gamecatcher só lê esses arquivos; o teste também.
4. Não rode o programa em laço nem agende execuções além das descritas aqui.
5. Não publique logs com tokens ou dados pessoais. A saída do gamecatcher não contém tokens, mas mostra o nome das contas Steam: pergunte ao usuário antes de colar a saída em qualquer lugar público.

## 1. Obter o binário

Opção A, recomendada: baixar o pacote compilado pelo CI.

```powershell
gh run download --repo matheus-fsc/gamecatcher -n windows -D gc-test
Expand-Archive gc-test\*.zip -DestinationPath gc-test
cd gc-test\gamecatcher-*-windows-x64
```

Se uma release já existir, também dá para baixar o `.zip` de https://github.com/matheus-fsc/gamecatcher/releases/latest.

Opção B: compilar do código-fonte (Visual Studio 2022 com "Desenvolvimento para desktop com C++" e Windows SDK, CMake 3.20+, vcpkg):

```powershell
git clone https://github.com/matheus-fsc/gamecatcher
cd gamecatcher
vcpkg install curl:x64-windows-static nlohmann-json:x64-windows-static
cmake -S . -B build -A x64 "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
copy build\Release\gamecatcher.exe .
```

Registre a versão do Windows (`winver` ou `[System.Environment]::OSVersion.Version`) e a saída de `.\gamecatcher.exe --version`.

## 2. Testes básicos (sem notificação)

| # | Comando | Esperado |
|---|---|---|
| 2.1 | `.\gamecatcher.exe --version` | `gamecatcher 1.0.0` |
| 2.2 | `.\gamecatcher.exe --help` | Texto de ajuda com acentos corretos (ex.: "notificação", "promoções") |
| 2.3 | `.\gamecatcher.exe list` | Primeira linha `conta: <nome da conta Steam em uso>`, depois a lista de promoções com status (`novo`, `na conta`, `sem base`...) |
| 2.4 | `.\gamecatcher.exe db` | `0 entradas em C:\Users\...\AppData\Local\gamecatcher\seen.db` (ou as entradas existentes) |

Se 2.3 não mostrar a conta certa, colete:

```powershell
Get-ItemProperty HKCU:\Software\Valve\Steam | Select-Object SteamPath, AutoLoginUser
Get-ItemProperty HKCU:\Software\Valve\Steam\ActiveProcess | Select-Object ActiveUser
Test-Path "$((Get-ItemProperty HKCU:\Software\Valve\Steam).SteamPath)\appcache\packageinfo.vdf"
```

## 3. Notificações (toast)

Antes de cada rodada, limpe o banco: `.\gamecatcher.exe db clear`.

Se `list` não mostrar nenhuma promoção como `novo`, use `--max 5` e, se ainda assim não houver nada, registre no relatório que não havia promoções disponíveis e pule para a seção 5.

| # | Ação | Esperado |
|---|---|---|
| 3.1 | `.\gamecatcher.exe` (deixe rodando) | Um toast por promoção nova, com o nome "gamecatcher", título "Jogo grátis na Steam" ou "DLC grátis na Steam", o texto com preço e prazo, `Conta: <nome>` e os botões **Resgatar** e **Ignorar** |
| 3.2 | Clique em **Ignorar** num toast | O terminal mostra `ignorado   <jogo>` |
| 3.3 | Feche outro toast pelo X | O terminal mostra `fechado    <jogo>` |
| 3.4 | Espere um toast ir para a Central de Ações sem responder; abra a Central (Win+N ou Win+A) e clique num botão lá | O clique funciona igual ao do toast na tela |
| 3.5 | Clique no corpo de um toast (fora dos botões) | Registre o que acontece (esperado: `fechado`, nunca `na fila`) |
| 3.6 | Quando todos forem respondidos | O programa termina sozinho; `.\gamecatcher.exe db` mostra os ignorados na conta certa |

Pontos de risco conhecidos, observe com atenção:

- **O toast não aparece:** o programa registra o AUMID `gamecatcher.notifier` em `HKCU\Software\Classes\AppUserModelId`. Verifique se a chave existe e se as notificações não estão bloqueadas em Configurações, Sistema, Notificações.
- **O toast aparece, mas os botões não fazem nada** (o terminal não mostra `ignorado`/`fechado`): é o risco principal desta versão. Ela depende do evento `Activated` do toast chegar ao processo, num app sem instalador (unpackaged). Registre exatamente o que aconteceu (o toast some? abre alguma janela? o processo continua rodando?).
- **Acentos quebrados** no terminal ou no toast.

## 4. Resgatar e fila de confirmação

Este teste precisa do usuário humano. Explique a ele o que vai acontecer e peça autorização.

| # | Ação | Esperado |
|---|---|---|
| 4.1 | Com a Steam **aberta**, clique em **Resgatar** num jogo que a conta não tem | A página do jogo abre no cliente Steam; o terminal mostra `na fila` e `aguardando "Adicionar à conta"` |
| 4.2 | Peça ao usuário para clicar em "Adicionar à conta" | Em poucos segundos: `confirmado  <jogo> (<conta>)` |
| 4.3 | Clique em **Resgatar** em dois toasts seguidos | A segunda página só abre depois que a primeira foi confirmada (ou após 10 minutos) |
| 4.4 | Com a Steam **fechada**, clique em **Resgatar** | A Steam abre já na página do jogo |

Se não houver promoção resgatável para a conta, registre e pule.

Observação: no Windows a fila não espera a Steam terminar de abrir (no Linux espera 40 s). Se em 4.4 com dois cliques a segunda página não abrir, registre: é uma melhoria conhecida a fazer.

## 5. Instalador e execução no logon

```powershell
powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1
```

| # | Ação | Esperado |
|---|---|---|
| 5.1 | Rodar o instalador | Mensagens com acentos corretos; `%LOCALAPPDATA%\gamecatcher\bin\gamecatcher.exe` existe |
| 5.2 | `Get-ScheduledTask gamecatcher \| Select-Object -ExpandProperty Triggers` | Gatilho de logon com atraso de 1 minuto |
| 5.3 | `.\gamecatcher.exe db clear` e depois `Start-ScheduledTask gamecatcher` | Os toasts aparecem **sem nenhuma janela de console** (o `conhost --headless` esconde) |
| 5.4 | Responda os toasts | `Get-ScheduledTaskInfo gamecatcher` mostra `LastTaskResult` 0 depois que todos forem respondidos |
| 5.5 | (Opcional, com autorização do usuário) sair e entrar de novo no Windows | Cerca de 1 minuto depois do logon, a verificação roda sozinha |
| 5.6 | `powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -Uninstall` | A tarefa e a pasta `bin` são removidas; o `seen.db` continua |

## 6. Relatório

Entregue ao usuário um relatório com:

1. Versão do Windows e do gamecatcher.
2. Uma tabela com cada item testado (2.1 a 5.6): **ok**, **falhou** ou **pulado**, com o motivo.
3. Para cada falha: o comando, a saída completa do terminal e o que apareceu na tela (print do toast, se possível).
4. Qualquer comportamento inesperado, mesmo que o teste tenha passado.

Não tente corrigir o código sem o usuário pedir. Se ele pedir, as partes específicas do Windows estão em `src/notify_windows.cpp` (toast e abertura da Steam), `src/steam_local.cpp` (registro e arquivos da Steam) e `packaging/windows/install.ps1`.
