#!/usr/bin/env bash

set -euo pipefail

VERSION="${SFETCH_VERSION:-v0.3}"
INSTALL_ROOT="${TMPDIR:-/tmp}/sfetch-install.$$"

cleanup() {
    rm -rf "$INSTALL_ROOT"
}
trap cleanup EXIT

if ! command -v curl >/dev/null 2>&1; then
    echo "Error: curl is required." >&2
    exit 1
fi
if ! command -v tar >/dev/null 2>&1; then
    echo "Error: tar is required to install ASCII assets." >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
elif command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
else
    echo "Error: run as root or install sudo." >&2
    exit 1
fi

run_privileged() {
    if [ -n "$SUDO" ]; then
        "$SUDO" "$@"
    else
        "$@"
    fi
}

if [ -n "${SFETCH_BINARY_URL:-}" ]; then
    RELEASE_BINARY="$SFETCH_BINARY_URL"
else
    case "$(uname -s):$(uname -m)" in
        Linux:x86_64|Linux:amd64)
            RELEASE_ASSET="sfetch-x86_amd64-linux"
            ;;
        FreeBSD:x86_64|FreeBSD:amd64|OpenBSD:x86_64|OpenBSD:amd64|NetBSD:x86_64|NetBSD:amd64|DragonFly:x86_64|DragonFly:amd64)
            RELEASE_ASSET="sfetch-x86_amd64-bsd"
            ;;
        Darwin:arm64|Darwin:aarch64)
            RELEASE_ASSET="sfetch-arm64-applesilicon-macos"
            ;;
        Darwin:x86_64|Darwin:amd64)
            RELEASE_ASSET="sfetch-macos-x86_amd64"
            ;;
        *)
            echo "Error: unsupported platform $(uname -s)/$(uname -m)." >&2
            exit 1
            ;;
    esac
    RELEASE_BINARY="https://github.com/Saaransh-Xd/fetch/releases/download/${VERSION}/${RELEASE_ASSET}"
fi

mkdir -p "$INSTALL_ROOT"

echo "Downloading sfetch ${VERSION} binary for $(uname -s)/$(uname -m)..."
curl -fL "$RELEASE_BINARY" -o "$INSTALL_ROOT/fetch"
chmod 755 "$INSTALL_ROOT/fetch"

echo "Downloading ASCII assets..."
ASSETS_ARCHIVE="$INSTALL_ROOT/assets.tar.gz"
curl -fL "https://github.com/Saaransh-Xd/fetch/archive/refs/tags/${VERSION}.tar.gz" \
    -o "$ASSETS_ARCHIVE"
tar -xzf "$ASSETS_ARCHIVE" -C "$INSTALL_ROOT"
ASSETS_SOURCE="$(find "$INSTALL_ROOT" -type d -path '*/assets/ascii' -print -quit)"
if [ -z "$ASSETS_SOURCE" ]; then
    echo "Error: release assets could not be located." >&2
    exit 1
fi

echo "Installing binary, assets and aliases..."
run_privileged mkdir -p /usr/local/bin
run_privileged install -m 755 "$INSTALL_ROOT/fetch" /usr/local/bin/sfetch
run_privileged ln -sf /usr/local/bin/sfetch /usr/local/bin/fetch
run_privileged mkdir -p /usr/local/share/sfetch/assets/ascii
run_privileged cp -R "$ASSETS_SOURCE"/. /usr/local/share/sfetch/assets/ascii/

run_privileged mkdir -p /etc/sfetch
if [ ! -e /etc/sfetch/config ]; then
    run_privileged tee /etc/sfetch/config >/dev/null <<'CONFIG'
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
    run_privileged chmod 644 /etc/sfetch/config
fi

echo "sfetch installed at /usr/local/bin/sfetch"
echo "Run: sfetch"
