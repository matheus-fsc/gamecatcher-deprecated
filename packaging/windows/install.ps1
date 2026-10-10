# Instala o gamecatcher e agenda 1 verificação a cada logon do usuário atual.
# Uso (PowerShell, sem admin):
#   powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 [-Binary build\Release\gamecatcher.exe]
#   powershell -ExecutionPolicy Bypass -File packaging\windows\install.ps1 -Uninstall
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
Copy-Item $Binary $Exe -Force
# DLLs do vcpkg ficam ao lado do .exe quando o build não é estático.
Get-ChildItem (Split-Path $Binary) -Filter *.dll -ErrorAction SilentlyContinue | Copy-Item -Destination $InstallDir -Force

# conhost --headless: sem janela de console (Windows 10 21H2+ / 11). O processo precisa ficar
# vivo até você responder os toasts, então uma janela visível ficaria aberta esse tempo todo.
$action = New-ScheduledTaskAction -Execute "conhost.exe" -Argument "--headless `"$Exe`" run"

# 1 min depois do logon, para a rede e o shell estarem prontos.
$trigger = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$trigger.Delay = "PT1M"

$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit (New-TimeSpan -Hours 12) `
    -MultipleInstances IgnoreNew

$principal = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Settings $settings `
    -Principal $principal -Description "Avisa quando um jogo pago fica grátis na Steam" -Force | Out-Null

Write-Host "instalado em $InstallDir"
Write-Host "tarefa '$TaskName': 1 verificação a cada logon"
