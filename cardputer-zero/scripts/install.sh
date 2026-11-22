#!/bin/sh
# SquachWatch-CM0 -- install on the Cardputer Zero (run ON the device, as
# root: `sudo sh install.sh [dir]`, where dir holds squachd,
# squachwatch.desktop and squachwatch.png; default: this script's dir).
#
#  /usr/local/bin/squachd   root:$GROUP 0750, cap_net_admin,cap_net_raw+ep
#     The sniffer needs exactly those two capabilities (monitor vif, channel
#     sets, raw packet + HCI sockets) and nothing else; everything else it
#     touches is the user's own (fb0/input/dialout groups, $HOME). Whoever
#     can run a CAP_NET_RAW binary can sniff, so only $GROUP may run it.
#     Replacing the file drops the capabilities: re-run this after every
#     update.
#  /usr/share/APPLaunch/applications/squachwatch.desktop + its icon: the
#     entry in the device's app launcher (Exec: squachd --fb).
#
# Uninstall: rm /usr/local/bin/squachd \
#   /usr/share/APPLaunch/applications/squachwatch.desktop \
#   /usr/share/APPLaunch/share/images/squachwatch.png
set -eu

GROUP=${SQUACH_GROUP:-netdev}
SRC=${1:-$(dirname "$0")}
APPL=/usr/share/APPLaunch

[ "$(id -u)" -eq 0 ] || { echo "run as root (sudo sh $0)" >&2; exit 1; }
for f in squachd squachwatch.desktop squachwatch.png; do
    [ -f "$SRC/$f" ] || { echo "missing $SRC/$f" >&2; exit 1; }
done
getent group "$GROUP" >/dev/null || { echo "no group $GROUP" >&2; exit 1; }
command -v setcap >/dev/null || { echo "setcap missing (apt install libcap2-bin)" >&2; exit 1; }

# Install to a temp name then rename: a running squachd keeps its old inode,
# and the capability is set before the new file becomes visible.
install -o root -g "$GROUP" -m 0750 "$SRC/squachd" /usr/local/bin/.squachd.new
setcap cap_net_admin,cap_net_raw+ep /usr/local/bin/.squachd.new
mv -f /usr/local/bin/.squachd.new /usr/local/bin/squachd

if [ -d "$APPL/applications" ]; then
    install -o root -g root -m 0644 "$SRC/squachwatch.desktop" "$APPL/applications/squachwatch.desktop"
    install -o root -g root -m 0644 "$SRC/squachwatch.png" "$APPL/share/images/squachwatch.png"
else
    echo "no $APPL: launcher entry skipped" >&2
fi

echo "installed:"
ls -l /usr/local/bin/squachd
getcap /usr/local/bin/squachd
for u in $(getent group "$GROUP" | cut -d: -f4 | tr , ' '); do
    for g in video input dialout; do
        id -nG "$u" | tr ' ' '\n' | grep -qx "$g" || echo "note: $u is not in group $g (needed for --fb / GPS)"
    done
done
