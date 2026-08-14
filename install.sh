#!/usr/bin/env bash

set -euo pipefail

case "$(uname -s)" in
    Linux) INSTALLER="install-linux.sh" ;;
    Darwin) INSTALLER="install-macos.sh" ;;
    *)
        echo "Error: unsupported operating system: $(uname -s)." >&2
        exit 1
        ;;
esac

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
if [ -f "$SCRIPT_DIR/$INSTALLER" ]; then
    exec bash "$SCRIPT_DIR/$INSTALLER"
fi

if ! command -v curl >/dev/null 2>&1; then
    echo "Error: curl is required." >&2
    exit 1
fi

curl -fsSL "https://raw.githubusercontent.com/Saaransh-Xd/fetch/main/${INSTALLER}" | bash
