#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
# Builds dist/RICOH-SP211-Native-<version>.dmg containing the installer package, the uninstaller app and the router AirPrint app.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=${VERSION:-1.1.0}
ID=com.github.creative8428.sp211native
OUT=dist; WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

# The Read Me names the commit the binaries are built from, so every disk image
# points at its own corresponding source (GPL v2 section 3). Push that commit.
if SOURCE_REF=$(git rev-parse --verify -q HEAD 2>/dev/null); then
  git diff --quiet HEAD || echo "warning: uncommitted changes; the source link in Read Me.txt will not match this build" >&2
else
  SOURCE_REF="v$VERSION"   # built outside git, e.g. from the source archive of that tag
fi

make -s
make -s router   # static AirPrint server for OpenWrt routers (needs zig)
./tests/run_tests.sh >/dev/null || { echo "tests failed"; exit 1; }

# payload
R=$WORK/root
install -d "$R/Library/Printers/SP211Native/Filters" "$R/Library/Printers/PPDs/Contents/Resources"
install -m 755 build/rastertosp211 "$R/Library/Printers/SP211Native/Filters/rastertosp211"
install -m 644 LICENSE "$R/Library/Printers/SP211Native/LICENSE"
gzip -9 -n -c ppd/RICOH-SP-211-native.ppd > "$R/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"
chmod 644 "$R/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"

pkgbuild --quiet --root "$R" --install-location / --ownership recommended \
  --scripts packaging/scripts-install --identifier $ID --version "$VERSION" "$WORK/sp211native.pkg"

sed "s/__VERSION__/$VERSION/" packaging/distribution-install.xml > "$WORK/dist-i.xml"
S=$WORK/dmg; mkdir -p "$S"
productbuild --quiet --distribution "$WORK/dist-i.xml" --resources packaging/resources \
  --package-path "$WORK" "$S/Install Driver.pkg"
# uninstaller: small AppleScript app that runs uninstall.sh as administrator
U="$S/Uninstall Driver.app"
osacompile -o "$U" packaging/uninstall.applescript
install -m 755 packaging/uninstall.sh "$U/Contents/Resources/uninstall.sh"
install -m 644 LICENSE "$U/Contents/Resources/LICENSE.txt"   # each app carries its license
codesign --force --deep --sign - "$U" 2>/dev/null
# router setup app: installs/removes the AirPrint server on an OpenWrt router over SSH
A="$S/AirPrint on Router.app"
osacompile -o "$A" packaging/router-app.applescript
install -m 755 packaging/router-run.sh packaging/router-askpass.sh "$A/Contents/Resources/"
install -m 644 build/router/sp211-airprint-router.tar.gz "$A/Contents/Resources/"
install -m 644 LICENSE "$A/Contents/Resources/LICENSE.txt"
install -m 644 build/THIRD-PARTY-LICENSES.txt "$A/Contents/Resources/THIRD-PARTY-LICENSES.txt"
codesign --force --deep --sign - "$A" 2>/dev/null

sed -e "s/__VERSION__/$VERSION/g" -e "s/__SOURCE_REF__/$SOURCE_REF/g" packaging/ReadMe.txt > "$S/Read Me.txt"
cp LICENSE "$S/License.txt"   # GPL v2: the license travels with the binaries
cp build/THIRD-PARTY-LICENSES.txt "$S/Third-Party Licenses.txt"   # musl and Zig runtime in the router binary

mkdir -p "$OUT"; DMG="$OUT/RICOH-SP211-Native-$VERSION.dmg"; rm -f "$DMG"
hdiutil create -quiet -volname "Ricoh SP 211 Driver" -srcfolder "$S" -fs HFS+ -format UDZO "$DMG"
echo "Built $DMG"
