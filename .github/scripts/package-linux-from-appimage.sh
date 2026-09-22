#!/usr/bin/env bash
# Build Debian, RPM and portable tar.gz client packages from the CI AppImage.
# Usage: package-linux-from-appimage.sh APPIMAGE OUTPUT_DIR VERSION [SOURCE_REPO]
set -euo pipefail

APPIMAGE=$(readlink -f "$1")
OUTPUT_DIR=$(readlink -f "$2")
RAW_VERSION=${3:-dev}
SRC_REPO=${4:-}
VERSION=${RAW_VERSION#v}
# Debian/RPM reject '-' inside upstream parts of the version and a prerelease
# must order BEFORE the stable release: 6.2.0-rc1 -> 6.2.0~rc1.
PKG_VERSION=$(printf '%s' "$VERSION" | sed -E 's/^[^0-9]*//; s/-/~/g')

command -v dpkg-deb >/dev/null
command -v rpmbuild >/dev/null
mkdir -p "$OUTPUT_DIR"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

chmod +x "$APPIMAGE"
(cd "$TMP" && "$APPIMAGE" --appimage-extract >/dev/null)
APPDIR="$TMP/squashfs-root"
test -x "$APPDIR/usr/bin/sufficit-phone"
test -f "$APPDIR/usr/share/applications/sufficit-phone.desktop"

# Mirror the AppImage layout into a private tree (/usr/lib/sufficit-phone).
# The executable's RUNPATH ($ORIGIN/../lib) and bin/qt.conf (Prefix=..) make
# this tree self-contained; base Qt/X11/ALSA libs keep coming from the distro.
PKG="$TMP/pkg"
STAGE="$PKG/usr/lib/sufficit-phone"
mkdir -p "$STAGE"
for d in bin lib plugins qml translations share; do
  [ -d "$APPDIR/usr/$d" ] && cp -a "$APPDIR/usr/$d" "$STAGE/"
done

# Public launcher and desktop integration in standard system paths.
mkdir -p "$PKG/usr/bin" "$PKG/usr/share/applications" "$PKG/usr/share/icons" "$PKG/usr/share/doc/sufficit-phone"
printf '#!/bin/sh\nexec /usr/lib/sufficit-phone/bin/sufficit-phone "$@"\n' > "$PKG/usr/bin/sufficit-phone"
chmod 0755 "$PKG/usr/bin/sufficit-phone"
cp "$APPDIR/usr/share/applications/sufficit-phone.desktop" "$PKG/usr/share/applications/"
cp -a "$APPDIR/usr/share/icons/hicolor" "$PKG/usr/share/icons/"
printf 'Sufficit Phone %s\nFork of linphone-desktop, GPLv3+.\nhttps://sufficit.com.br\n' "$RAW_VERSION" > "$PKG/usr/share/doc/sufficit-phone/copyright"
if [ -n "$SRC_REPO" ] && [ -f "$SRC_REPO/LICENSE.txt" ]; then
  cp "$SRC_REPO/LICENSE.txt" "$PKG/usr/share/doc/sufficit-phone/LICENSE.txt"
fi

# ---------- Debian ----------
DEBROOT="$TMP/deb"
mkdir -p "$DEBROOT/DEBIAN"
cp -a "$PKG/usr" "$DEBROOT/"
cat > "$DEBROOT/DEBIAN/control" <<CONTROL
Package: sufficit-phone
Version: $PKG_VERSION
Architecture: amd64
Maintainer: Sufficit <hugodeco@sufficit.com.br>
Section: net
Priority: optional
Homepage: https://sufficit.com.br
Depends: libc6, libasound2t64 | libasound2, libbz2-1.0, libcom-err2, libegl1, libexpat1, libfontconfig1, libfreetype6, libgcc-s1, libgl1, libglvnd0, libglx0, libgpg-error0, libopengl0, libpng16-16, libstdc++6, libx11-6, libx11-xcb1, libxau6, libxcb1, libxdmcp6, zlib1g
Description: Sufficit Phone - SIP client
 Desktop SIP phone from Sufficit with audio/video, chat and bundled codecs.
CONTROL
cat > "$DEBROOT/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true
fi
POSTINST
chmod 0755 "$DEBROOT/DEBIAN/postinst"
dpkg-deb --root-owner-group --build "$DEBROOT" "$OUTPUT_DIR/sufficit-phone_${PKG_VERSION}_amd64.deb" >/dev/null

# ---------- RPM ----------
RPMTOP="$TMP/rpm"
mkdir -p "$RPMTOP/BUILD" "$RPMTOP/BUILDROOT" "$RPMTOP/RPMS/x86_64" "$RPMTOP/SOURCES" "$RPMTOP/SPECS" "$RPMTOP/SRPMS"
cat > "$RPMTOP/SPECS/sufficit-phone.spec" <<SPEC
Name: sufficit-phone
Version: $PKG_VERSION
Release: 1
Summary: Sufficit Phone - SIP client
License: GPL-3.0-or-later
URL: https://sufficit.com.br
BuildArch: x86_64
AutoReqProv: no
Requires: libX11.so.6()(64bit), libX11-xcb.so.1()(64bit), libxcb.so.1()(64bit), libGL.so.1()(64bit), libEGL.so.1()(64bit), libfontconfig.so.1()(64bit), libfreetype.so.6()(64bit), libasound.so.2()(64bit), libpng16.so.16()(64bit), libexpat.so.1()(64bit), libcom_err.so.2()(64bit), libgpg-error.so.0()(64bit), libz.so.1()(64bit), libstdc++.so.6()(64bit), libgcc_s.so.1()(64bit)

%description
Desktop SIP phone from Sufficit with audio/video, chat and bundled codecs.

%prep
%build
%install
cp -a "$PKG/usr" %{buildroot}/usr

%files
/usr/bin/sufficit-phone
/usr/lib/sufficit-phone/
/usr/share/applications/sufficit-phone.desktop
/usr/share/icons/hicolor/
/usr/share/doc/sufficit-phone/
SPEC
rpmbuild -bb --define "_topdir $RPMTOP" --buildroot "$RPMTOP/BUILDROOT/sufficit-phone" "$RPMTOP/SPECS/sufficit-phone.spec" >/dev/null 2>&1
RPM_FILE=$(find "$RPMTOP/RPMS" -type f -name '*.rpm' -print -quit)
test -n "$RPM_FILE"
cp "$RPM_FILE" "$OUTPUT_DIR/sufficit-phone-${PKG_VERSION}-1.x86_64.rpm"

# ---------- Portable tar.gz ----------
TAR_NAME="sufficit-phone-${PKG_VERSION}-linux-x86_64"
TARROOT="$TMP/$TAR_NAME"
mkdir -p "$TARROOT"
cp -a "$PKG/usr" "$TARROOT/usr"
cat > "$TARROOT/install.sh" <<'INSTALL'
#!/bin/sh
set -eu
[ "$(id -u)" = 0 ] || { echo "Run with sudo: sudo ./install.sh" >&2; exit 1; }
cd "$(dirname "$0")"
cp -a usr/bin/sufficit-phone /usr/bin/
mkdir -p /usr/lib/sufficit-phone /usr/share/applications /usr/share/icons /usr/share/doc/sufficit-phone
cp -a usr/lib/sufficit-phone/. /usr/lib/sufficit-phone/
cp -a usr/share/applications/. /usr/share/applications/
cp -a usr/share/icons/. /usr/share/icons/
cp -a usr/share/doc/sufficit-phone/. /usr/share/doc/sufficit-phone/
update-desktop-database -q /usr/share/applications 2>/dev/null || true
echo "Sufficit Phone installed. Run: sufficit-phone"
INSTALL
chmod 0755 "$TARROOT/install.sh"
printf 'Sufficit Phone %s for Linux x86_64\n\nInstall:  sudo ./install.sh\nRemove:   sudo rm -rf /usr/lib/sufficit-phone /usr/bin/sufficit-phone /usr/share/applications/sufficit-phone.desktop\n' "$RAW_VERSION" > "$TARROOT/README.txt"
tar -C "$TMP" -czf "$OUTPUT_DIR/${TAR_NAME}.tar.gz" "$TAR_NAME"

printf 'Generated Linux client packages:\n'
ls -lh "$OUTPUT_DIR"/sufficit-phone_*.deb "$OUTPUT_DIR"/sufficit-phone-*.rpm "$OUTPUT_DIR"/sufficit-phone-*.tar.gz
