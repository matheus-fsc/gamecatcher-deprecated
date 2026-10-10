@echo off
rem Remove o gamecatcher e a tarefa agendada (o banco seen.db fica): dois cliques.
setlocal
set "ps1=%~dp0packaging\windows\install.ps1"
if not exist "%ps1%" set "ps1=%~dp0install.ps1"
powershell -NoProfile -ExecutionPolicy Bypass -File "%ps1%" -Uninstall
pause
