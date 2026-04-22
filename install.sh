#!/usr/bin/env bash
# install.sh — build and install Kiln Terminal from source.
#
# Usage:
#   ./install.sh                 # installs to /usr/local (needs sudo)
#   ./install.sh --user          # installs to ~/.local (no sudo)
#   ./install.sh --prefix=/opt/kiln
#   ./install.sh --uninstall     # remove a previous install
#
# This is the "quick" path for users who just want a working binary. To build
# a real .deb package use packaging/debian/ (see README for `dpkg-buildpackage`
# instructions).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"

PREFIX=/usr/local
USER_INSTALL=0
UNINSTALL=0

for arg in "$@"; do
    case "$arg" in
        --user)         USER_INSTALL=1 ;;
        --prefix=*)     PREFIX="${arg#--prefix=}" ;;
        --uninstall)    UNINSTALL=1 ;;
        -h|--help)
            sed -n '2,14p' "$0"; exit 0 ;;
        *)
            echo "Unknown argument: $arg" >&2; exit 2 ;;
    esac
done

if [[ $USER_INSTALL -eq 1 ]]; then
    PREFIX="$HOME/.local"
fi

SUDO=
if [[ ! -w "$PREFIX" && $USER_INSTALL -eq 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO=sudo
    else
        echo "Need write access to $PREFIX and sudo is not installed." >&2
        exit 1
    fi
fi

check_dep() {
    if ! pkg-config --exists "$1"; then
        echo "Missing build dependency: $1" >&2
        echo "Install with: sudo apt install libgtk-4-dev libadwaita-1-dev libvte-2.91-gtk4-dev cmake pkg-config build-essential" >&2
        exit 1
    fi
}

if [[ $UNINSTALL -eq 1 ]]; then
    MANIFEST="$BUILD/install_manifest.txt"
    if [[ ! -f "$MANIFEST" ]]; then
        echo "No install_manifest.txt found in $BUILD — was this installed via this script?" >&2
        exit 1
    fi
    echo "Removing files listed in $MANIFEST …"
    while IFS= read -r f; do
        [[ -n "$f" ]] && $SUDO rm -f -- "$f"
    done < "$MANIFEST"
    $SUDO gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" 2>/dev/null || true
    $SUDO update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
    echo "Uninstalled."
    exit 0
fi

check_dep gtk4
check_dep libadwaita-1
check_dep vte-2.91-gtk4

cmake -S "$ROOT" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build "$BUILD" -j"$(nproc)"

$SUDO cmake --install "$BUILD"

# Refresh desktop / icon caches, if available, so the launcher shows up.
$SUDO update-desktop-database -q "$PREFIX/share/applications" 2>/dev/null || true
$SUDO gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" 2>/dev/null || true

echo
echo "Kiln Terminal installed to $PREFIX."
echo "Binary:  $PREFIX/bin/kiln-terminal"
echo "Desktop: $PREFIX/share/applications/dev.kiln.Terminal.desktop"
if [[ "$PREFIX" = "$HOME/.local" ]]; then
    case ":$PATH:" in
        *":$HOME/.local/bin:"*) ;;
        *) echo "Note: add \"\$HOME/.local/bin\" to your PATH to use 'kiln-terminal' from any shell." ;;
    esac
fi
