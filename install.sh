#!/usr/bin/env bash

set -euo pipefail

REPOSITORY="${SFETCH_REPOSITORY:-https://github.com/Saaransh-Xd/fetch.git}"
VERSION="${SFETCH_VERSION:-v0.2}"
RELEASE_BINARY="${SFETCH_BINARY_URL:-https://github.com/Saaransh-Xd/fetch/releases/download/${VERSION}/fetch}"
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

echo "Cloning $REPOSITORY..."
git clone --depth 1 "$REPOSITORY" "$INSTALL_ROOT"

echo "Downloading sfetch ${VERSION} binary..."
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
