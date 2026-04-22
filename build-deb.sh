#!/usr/bin/env bash
# build-deb.sh — build a .deb using the skeleton under packaging/debian/.
#
# This copies the packaging/debian/ tree to ./debian/ (which dpkg-buildpackage
# requires at the source root), runs the build, and leaves the resulting
# artifacts in ../.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if ! command -v dpkg-buildpackage >/dev/null 2>&1; then
    echo "dpkg-buildpackage not found. Install with:" >&2
    echo "  sudo apt install devscripts debhelper dpkg-dev" >&2
    exit 1
fi

# Stage debian/ at the root, since dpkg-buildpackage wants it there.
rm -rf debian
cp -a packaging/debian debian
chmod +x debian/rules

dpkg-buildpackage -us -uc -b

echo
echo "Built .deb artifacts in parent directory:"
ls -1 ../kiln-terminal_*.deb 2>/dev/null || true
