#!/bin/sh
# Installs LiveRack for the current user (no root needed):
#   ~/.local/opt/LiveRack, a launcher in the applications menu, and a check of
#   the real-time permissions live audio needs.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
dest="$HOME/.local/opt/LiveRack"
apps="$HOME/.local/share/applications"

mkdir -p "$dest" "$apps"
cp "$here/LiveRack" "$here/LiveRackScanner" "$here/LiveRackGuard" "$here/AppIcon.png" "$dest/"
chmod +x "$dest/LiveRack" "$dest/LiveRackScanner" "$dest/LiveRackGuard"

sed -e "s|@DEST@|$dest|g" "$here/liverack.desktop" > "$apps/liverack.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" || true

echo "Installed to $dest"

# Live audio needs real-time scheduling; without it, dropouts under load.
rtprio=$(ulimit -r 2>/dev/null || echo 0)
if [ "$rtprio" = "0" ]; then
    echo
    echo "Real-time priority is not allowed for $(id -un) yet. To allow it:"
    echo "  sudo usermod -aG audio $(id -un)"
    echo "  echo '@audio - rtprio 95' | sudo tee /etc/security/limits.d/99-liverack.conf"
    echo "  echo '@audio - memlock unlimited' | sudo tee -a /etc/security/limits.d/99-liverack.conf"
    echo "then log out and back in. (Many audio distros already do this.)"
fi
