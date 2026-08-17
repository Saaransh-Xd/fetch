#!/usr/bin/env bash

set -euo pipefail

VERSION="${larpfetch_VERSION:-v0.4}"
REPOSITORY="${larpfetch_REPOSITORY:-Saaransh-Xd/larpfetch}"
INSTALL_ROOT="${TMPDIR:-/tmp}/larpfetch-install.$$"
RELEASE_ASSET="larpfetch-x86_amd64-bsd"

cleanup() { rm -rf "$INSTALL_ROOT"; }
trap cleanup EXIT

if ! command -v curl >/dev/null 2>&1 || ! command -v tar >/dev/null 2>&1; then
    echo "Error: curl and tar are required." >&2
    exit 1
fi
case "$(uname -s):$(uname -m)" in
    FreeBSD:x86_64|FreeBSD:amd64|OpenBSD:x86_64|OpenBSD:amd64|NetBSD:x86_64|NetBSD:amd64|DragonFly:x86_64|DragonFly:amd64) ;;
    *) echo "Error: this release only supports BSD x86_64/amd64." >&2; exit 1 ;;
esac

if [ "$(id -u)" -eq 0 ]; then SUDO="";
elif command -v sudo >/dev/null 2>&1; then SUDO="sudo";
else echo "Error: run as root or install sudo." >&2; exit 1; fi

run_privileged() { if [ -n "$SUDO" ]; then "$SUDO" "$@"; else "$@"; fi; }

RELEASE_BINARY="${LARPFETCH_BINARY_URL:-https://github.com/${REPOSITORY}/releases/download/${VERSION}/${RELEASE_ASSET}}"
mkdir -p "$INSTALL_ROOT"
echo "Downloading larpfetch ${VERSION} for $(uname -s) x86_64..."
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
# larpfetch configuration: use true/false to toggle sections.
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
fps=10
infinite=true
frames=48
CONFIG
    run_privileged chmod 644 /etc/larpfetch/config
fi
echo "larpfetch installed at /usr/local/bin/larpfetch"
