@echo off
rem Instala (ou atualiza) o gamecatcher para o usuario atual, sem administrador: dois cliques.
rem Chama o install.ps1 com -ExecutionPolicy Bypass, entao nao esbarra na politica de scripts.
setlocal
set "ps1=%~dp0packaging\windows\install.ps1"
if not exist "%ps1%" set "ps1=%~dp0install.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ps1%" %*
pause
