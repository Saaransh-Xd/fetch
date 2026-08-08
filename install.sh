#!/usr/bin/env bash

set -euo pipefail

REPOSITORY="${SFETCH_REPOSITORY:-https://github.com/Saaransh-Xd/fetch.git}"
VERSION="${SFETCH_VERSION:-v0.3}"
INSTALL_ROOT="${TMPDIR:-/tmp}/sfetch-install.$$"

cleanup() {
    rm -rf "$INSTALL_ROOT"
}
trap cleanup EXIT

if ! command -v git >/dev/null 2>&1; then
    echo "Error: git is required." >&2
    exit 1
fi
if ! command -v curl >/dev/null 2>&1; then
    echo "Error: curl is required." >&2
    exit 1
fi
if ! command -v sudo >/dev/null 2>&1; then
    echo "Error: sudo is required for a global installation." >&2
    exit 1
fi

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

echo "Cloning $REPOSITORY..."
git clone --depth 1 "$REPOSITORY" "$INSTALL_ROOT"

echo "Downloading sfetch ${VERSION} binary for $(uname -s)/$(uname -m)..."
curl -fL "$RELEASE_BINARY" -o "$INSTALL_ROOT/fetch"
chmod 755 "$INSTALL_ROOT/fetch"

echo "Installing binary, assets and aliases..."
sudo install -Dm755 "$INSTALL_ROOT/fetch" /usr/local/bin/sfetch
sudo ln -sf /usr/local/bin/sfetch /usr/local/bin/fetch
sudo install -d -m 755 /usr/local/share/sfetch/assets
sudo cp -R "$INSTALL_ROOT/assets/ascii" /usr/local/share/sfetch/assets/

sudo install -d -m 755 /etc/sfetch
if [ ! -e /etc/sfetch/config ]; then
    sudo tee /etc/sfetch/config >/dev/null <<'CONFIG'
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
    sudo chmod 644 /etc/sfetch/config
fi

echo "sfetch installed at /usr/local/bin/sfetch"
echo "Run: sfetch"
