#!/usr/bin/env bash
# Instala o gamecatcher em ~/.local/bin e agenda 1 verificação a cada login na área de trabalho
# (XDG autostart: KDE, GNOME, XFCE...).
# Uso: packaging/linux/install.sh [caminho/do/binario]
#      (padrão: ./gamecatcher do pacote da release, ou build/gamecatcher no código-fonte)
#      packaging/linux/install.sh --uninstall
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
autostart="${XDG_CONFIG_HOME:-$HOME/.config}/autostart/gamecatcher.desktop"

if [[ "${1:-}" == "--uninstall" ]]; then
    rm -fv "$autostart" "$HOME/.local/bin/gamecatcher"
    exit 0
fi

default_bin="$root/gamecatcher"
[[ -x "$default_bin" ]] || default_bin="$root/build/gamecatcher"
bin="${1:-$default_bin}"
[[ -x "$bin" ]] || { echo "binário não encontrado: $bin (rode cmake --build build)" >&2; exit 1; }

install -Dm755 "$bin" "$HOME/.local/bin/gamecatcher"
mkdir -p "$(dirname "$autostart")"
sed "s|@BIN@|$HOME/.local/bin/gamecatcher|" "$root/packaging/linux/gamecatcher.desktop" > "$autostart"
echo "instalado: roda 1 verificação a cada login ($autostart)"
