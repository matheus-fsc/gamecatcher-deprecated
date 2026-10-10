#!/usr/bin/env bash
# Instala (ou atualiza) o gamecatcher para o usuário atual: copia para ~/.local/bin e agenda 1
# verificação a cada login (XDG autostart) e a cada 6 horas (timer do systemd do usuário).
# Quem faz isso é o próprio programa (gamecatcher install); este script só acha o binário.
# Uso: packaging/linux/install.sh [caminho/do/binario]
#      (padrão: ./gamecatcher do pacote da release, ou build/gamecatcher no código-fonte)
#      packaging/linux/install.sh --uninstall
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"

if [[ "${1:-}" == "--uninstall" ]]; then
    exec "$HOME/.local/bin/gamecatcher" uninstall
fi

default_bin="$root/gamecatcher"
[[ -x "$default_bin" ]] || default_bin="$root/build/gamecatcher"
bin="${1:-$default_bin}"
[[ -x "$bin" ]] || { echo "binário não encontrado: $bin (rode cmake --build build)" >&2; exit 1; }

exec "$bin" install
