#!/usr/bin/env bash

set -euo pipefail

VERSION="${SFETCH_VERSION:-v0.4}"
REPOSITORY="${SFETCH_REPOSITORY:-Saaransh-Xd/larpfetch}"
INSTALL_ROOT="${TMPDIR:-/tmp}/larpfetch-install.$$"
RELEASE_ASSET="sfetch-x86_amd64-linux"

cleanup() { rm -rf "$INSTALL_ROOT"; }
trap cleanup EXIT

if ! command -v curl >/dev/null 2>&1 || ! command -v tar >/dev/null 2>&1; then
    echo "Error: curl and tar are required." >&2
    exit 1
fi
if [ "$(uname -m)" != "x86_64" ] && [ "$(uname -m)" != "amd64" ]; then
    echo "Error: this release only supports Linux x86_64/amd64." >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then SUDO="";
elif command -v sudo >/dev/null 2>&1; then SUDO="sudo";
else echo "Error: run as root or install sudo." >&2; exit 1; fi

run_privileged() { if [ -n "$SUDO" ]; then "$SUDO" "$@"; else "$@"; fi; }

RELEASE_BINARY="${SFETCH_BINARY_URL:-https://github.com/${REPOSITORY}/releases/download/${VERSION}/${RELEASE_ASSET}}"
mkdir -p "$INSTALL_ROOT"
echo "Downloading sfetch ${VERSION} for Linux x86_64..."
curl -fL "$RELEASE_BINARY" -o "$INSTALL_ROOT/fetch"
curl -fL "https://github.com/${REPOSITORY}/archive/refs/tags/${VERSION}.tar.gz" -o "$INSTALL_ROOT/assets.tar.gz"
tar -xzf "$INSTALL_ROOT/assets.tar.gz" -C "$INSTALL_ROOT"
ASSETS_SOURCE="$(find "$INSTALL_ROOT" -type d -path '*/assets/ascii' -print)"
[ -n "$ASSETS_SOURCE" ] || { echo "Error: ASCII assets not found." >&2; exit 1; }
LARP_SOURCE_FILE="$(find "$INSTALL_ROOT" -type f -name larp.py -print | head -n 1)"
[ -n "$LARP_SOURCE_FILE" ] || { echo "Error: LARP script not found." >&2; exit 1; }
LARP_SOURCE="$(dirname "$LARP_SOURCE_FILE")"

chmod 755 "$INSTALL_ROOT/fetch"
run_privileged mkdir -p /usr/local/bin /usr/local/share/larpfetch/assets/ascii /usr/local/share/larpfetch/larp /etc/larpfetch
run_privileged install -m 755 "$INSTALL_ROOT/fetch" /usr/local/bin/larpfetch
run_privileged ln -sf /usr/local/bin/larpfetch /usr/local/bin/fetch
run_privileged cp -R "$ASSETS_SOURCE"/. /usr/local/share/larpfetch/assets/ascii/
run_privileged cp -R "$LARP_SOURCE"/. /usr/local/share/larpfetch/larp/

if [ ! -e /etc/larpfetch/config ]; then
    run_privileged tee /etc/larpfetch/config >/dev/null <<'CONFIG'
# sfetch configuration: use true/false to toggle sections.
logo=true
header=true
os=true
kernel=true
uptime=true
cpu=true
gpu=true
memory=true
disks=true
swap=true
packages=true
terminal=true
local_ip=true
display=true
battery=true
chassis=true
processes=true
arch=true
shell=true
palette=true
CONFIG
    run_privileged chmod 644 /etc/larpfetch/config
fi
echo "larpfetch installed at /usr/local/bin/larpfetch"
