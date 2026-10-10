# Instala o gamecatcher e agenda 1 verificação a cada logon do usuário atual e a cada 6 horas
# (para quem deixa o PC ligado). Rodar de novo atualiza a instalação.
# Uso (PowerShell, sem admin):
#   powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 [-Binary build\Release\gamecatcher.exe]
#   powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -Uninstall
# Mais simples: dois cliques no install.bat / uninstall.bat, que chamam este script.
#
# Quem cria a tarefa é este script (o PowerShell é assinado pela Microsoft), não o gamecatcher.exe:
# um .exe sem assinatura que cria a própria tarefa de logon é bloqueado pelo Defender.
param(
    [string]$Binary = "",
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

$TaskName = "gamecatcher"
$InstallDir = Join-Path $env:LOCALAPPDATA "gamecatcher\bin"
$Exe = Join-Path $InstallDir "gamecatcher.exe"

if ($Uninstall) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    # Nome das notificações, registrado pelo programa.
    Remove-Item "HKCU:\Software\Classes\AppUserModelId\gamecatcher.notifier" -Recurse -ErrorAction SilentlyContinue
    Write-Host "removido (o banco seen.db em $env:LOCALAPPDATA\gamecatcher foi mantido)"
    return
}

if (-not $Binary) {
    # Pacote da release: gamecatcher.exe na raiz. Código-fonte: build\Release\gamecatcher.exe.
    $Binary = Join-Path $PSScriptRoot "..\..\gamecatcher.exe"
    if (-not (Test-Path $Binary)) { $Binary = Join-Path $PSScriptRoot "..\..\build\Release\gamecatcher.exe" }
}
if (-not (Test-Path $Binary)) { throw "binário não encontrado: $Binary (compile antes)" }
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
# Se uma verificação estiver rodando (esperando resposta nos toasts), o .exe está em uso: não dá
# para sobrescrever, mas dá para renomear. O programa apaga o .old na próxima execução.
if (Test-Path $Exe) { Move-Item $Exe "$Exe.old" -Force }
Copy-Item $Binary $Exe -Force
# DLLs do vcpkg ficam ao lado do .exe quando o build não é estático.
Get-ChildItem (Split-Path $Binary) -Filter *.dll -ErrorAction SilentlyContinue | Copy-Item -Destination $InstallDir -Force

# conhost --headless: sem janela de console (Windows 10 21H2+ / 11). O processo precisa ficar
# vivo até você responder os toasts, então uma janela visível ficaria aberta esse tempo todo.
$action = New-ScheduledTaskAction -Execute "conhost.exe" -Argument "--headless `"$Exe`" run"

# 1 min depois do logon, para a rede e o shell estarem prontos.
$logon = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$logon.Delay = "PT1M"
# E a cada 6 h, para o PC que fica ligado. Uma execução que ainda espera resposta nos toasts não
# é duplicada (IgnoreNew abaixo e a trava do próprio programa).
$every6h = New-ScheduledTaskTrigger -Once -At (Get-Date).AddHours(6) -RepetitionInterval (New-TimeSpan -Hours 6)

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -StartWhenAvailable `
    -ExecutionTimeLimit (New-TimeSpan -Hours 12) `
    -MultipleInstances IgnoreNew

$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $logon, $every6h -Settings $settings `
    -Principal $principal -Description "Avisa quando um jogo pago fica grátis na Steam" -Force | Out-Null

Write-Host "instalado em $InstallDir"
Write-Host "tarefa '$TaskName': 1 verificação a cada logon e a cada 6 horas"
