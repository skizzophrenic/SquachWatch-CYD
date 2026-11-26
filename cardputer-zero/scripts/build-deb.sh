#!/bin/sh
# SquachWatch for the Cardputer Zero -- build the Debian package.
#
#   scripts/build-deb.sh [binary] [outdir]      (or: make deb)
#
# Run in the arm64 build environment (docker/Dockerfile) after `make`. It
# packages the built binary (default ./squachd) into
#   <outdir>/squachwatch_<version>_arm64.deb      (default outdir: dist/)
# which installs exactly what README.md's manual install does:
#   /usr/bin/squachd                         root:netdev 0750, with the two
#                                            capabilities the sniffer needs
#   /usr/share/APPLaunch/applications/squachwatch.desktop   the launcher entry
#   /usr/share/APPLaunch/share/images/squachwatch.png       its icon
# Install on the device:  sudo apt install ./squachwatch_<version>_arm64.deb
# Remove:                 sudo apt remove squachwatch
#
# The version is the firmware's release plus the commit it was built from:
#   1.49.0+cm0.3.g7645468   (commits since the tag; ".dirty" when built from
# uncommitted changes). DEB_VERSION=... overrides it. The Maintainer field
# comes from the git user (DEB_MAINTAINER=... overrides it).
set -eu

cd "$(dirname "$0")/.."
BIN=${1:-squachd}
OUT=${2:-dist}
NAME=squachwatch
APPL=/usr/share/APPLaunch

die() { echo "build-deb: $*" >&2; exit 1; }

[ -f "$BIN" ] || die "no $BIN -- run make first"
command -v dpkg-deb >/dev/null || die "dpkg-deb missing (build in the Debian container, docker/Dockerfile)"
ARCH=$(dpkg --print-architecture)
[ "$ARCH" = arm64 ] || die "this is a $ARCH system; build in the arm64 container so the package matches the device"
objdump -f "$BIN" 2>/dev/null | grep -q aarch64 || die "$BIN is not an aarch64 binary"

# ---- the version -----------------------------------------------------------
if [ -n "${DEB_VERSION:-}" ]; then
    VERSION=$DEB_VERSION
else
    tag=$(git describe --tags --abbrev=0 2>/dev/null || echo v0.0.0)
    fw=${tag#v}
    n=$(git rev-list --count "$tag..HEAD" 2>/dev/null || echo 0)
    sha=$(git rev-parse --short HEAD 2>/dev/null || echo unknown)
    VERSION="$fw+cm0.$n.g$sha"
    git diff --quiet HEAD -- 2>/dev/null || VERSION="$VERSION.dirty"
fi
MAINT=${DEB_MAINTAINER:-"$(git config user.name 2>/dev/null || echo SquachWatch) <$(git config user.email 2>/dev/null || echo noreply@localhost)>"}

# ---- what it needs on the device -------------------------------------------
# The binary's shared libraries, by the package that ships each one here
# (the container is the device's Debian release, so the names match), plus
# the tools it runs or the install uses: iw and ip (the monitor interface and
# channel hopping), setcap (postinst). No version pins: within one Debian
# release these are ABI-stable, and a +debNuN revision difference between
# the container and the device must not block the install.
deps=""
for lib in $(objdump -p "$BIN" | awk '/NEEDED/ { print $2 }'); do
    pkg=$(dpkg -S "*/$lib" 2>/dev/null | head -1 | cut -d: -f1)
    [ -n "$pkg" ] || die "no package owns $lib"
    case " $deps " in *" $pkg "*) ;; *) deps="$deps $pkg" ;; esac
done
DEPENDS=$(echo $deps iw iproute2 libcap2-bin | tr ' ' '\n' | sort -u | paste -sd, - | sed 's/,/, /g')

# ---- the tree --------------------------------------------------------------
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
ROOT=$STAGE/$NAME
mkdir -p "$ROOT/DEBIAN" "$ROOT/usr/bin" "$ROOT$APPL/applications" "$ROOT$APPL/share/images" \
         "$ROOT/usr/share/doc/$NAME"

# 0755 in the package; postinst narrows it to root:netdev 0750 + the caps.
# Stripped of its debug info, as Debian ships binaries (the build's own
# ./squachd keeps it, for gdb): ~22 MB -> a few MB on the device.
install -m 0755 "$BIN" "$ROOT/usr/bin/squachd"
strip --strip-debug "$ROOT/usr/bin/squachd"
sed 's|^Exec=.*|Exec=/usr/bin/squachd --fb|' assets/squachwatch.desktop > "$ROOT$APPL/applications/squachwatch.desktop"
chmod 0644 "$ROOT$APPL/applications/squachwatch.desktop"
install -m 0644 assets/squachwatch.png "$ROOT$APPL/share/images/squachwatch.png"
install -m 0644 README.md "$ROOT/usr/share/doc/$NAME/README.md"

SIZE=$(du -sk "$ROOT" | cut -f1)
cat > "$ROOT/DEBIAN/control" <<EOF
Package: $NAME
Version: $VERSION
Architecture: $ARCH
Maintainer: $MAINT
Installed-Size: $SIZE
Depends: $DEPENDS
Section: net
Priority: optional
Description: SquachWatch for the M5Stack Cardputer Zero
 Passive Wi-Fi and Bluetooth watcher -- surveillance cameras, trackers,
 deauth floods, wardriving -- with the SquachWatch UI, as an app in the
 Cardputer Zero's launcher. Wi-Fi sniffing needs a USB adapter with
 monitor mode; see /usr/share/doc/squachwatch/README.md.
EOF

cat > "$ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
# squachwatch postinst: who may run the sniffer, and the two capabilities it
# needs (a monitor interface + channel sets, raw packet + HCI sockets).
# Whoever can run a CAP_NET_RAW binary can sniff, so only group netdev may.
set -e
BIN=/usr/bin/squachd
GROUP=netdev
case "$1" in
configure)
    if getent group "$GROUP" >/dev/null; then
        chown root:"$GROUP" "$BIN"
        chmod 0750 "$BIN"
    else
        echo "squachwatch: no group $GROUP -- $BIN left root-only (0700)" >&2
        chown root:root "$BIN"
        chmod 0700 "$BIN"
    fi
    setcap cap_net_admin,cap_net_raw+ep "$BIN"
    users=$(getent group "$GROUP" | cut -d: -f4 | tr , ' ')
    [ -n "$users" ] || echo "squachwatch: nobody is in group $GROUP yet: sudo usermod -aG $GROUP <user>" >&2
    for u in $users; do
        for g in video input dialout audio; do
            id -nG "$u" 2>/dev/null | tr ' ' '\n' | grep -qx "$g" ||
                echo "squachwatch: note: $u is not in group $g (video: screen, input: keys, dialout: GPS, audio: sound)" >&2
        done
    done
    if [ -e /usr/local/bin/squachd ]; then
        echo "squachwatch: an older manual install is at /usr/local/bin/squachd; the launcher" >&2
        echo "squachwatch: entry now runs $BIN. Remove the old copy: sudo rm /usr/local/bin/squachd" >&2
    fi
    echo "squachwatch: installed. If SquachWatch is not in the launcher yet: systemctl --user restart APPLaunch.service"
    ;;
esac
exit 0
EOF
chmod 0755 "$ROOT/DEBIAN/postinst"

( cd "$ROOT" && find . -type f ! -path './DEBIAN/*' -printf '%P\n' | sort | xargs md5sum ) > "$ROOT/DEBIAN/md5sums"

mkdir -p "$OUT"
DEB="$OUT/${NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group -Zxz --build "$ROOT" "$DEB" >/dev/null
echo "built $DEB"
echo "  Version: $VERSION"
echo "  Depends: $DEPENDS"
echo "install on the device: sudo apt install ./$(basename "$DEB")"
