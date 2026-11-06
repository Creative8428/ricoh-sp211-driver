#!/bin/bash
# Builds dist/RICOH-SP211-Native-<version>.dmg containing the installer package, the uninstaller app and the router AirPrint app.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=${VERSION:-1.1.0}
ID=com.github.creative8428.sp211native
OUT=dist; WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT

make -s
make -s router   # static AirPrint server for OpenWrt routers (needs zig)
./tests/run_tests.sh >/dev/null || { echo "tests failed"; exit 1; }

# payload
R=$WORK/root
install -d "$R/Library/Printers/SP211Native/Filters" "$R/Library/Printers/PPDs/Contents/Resources"
install -m 755 build/rastertosp211 "$R/Library/Printers/SP211Native/Filters/rastertosp211"
gzip -9 -n -c ppd/RICOH-SP-211-native.ppd > "$R/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"
chmod 644 "$R/Library/Printers/PPDs/Contents/Resources/RICOH SP 211 native.ppd.gz"

pkgbuild --quiet --root "$R" --install-location / --ownership recommended \
  --scripts packaging/scripts-install --identifier $ID --version "$VERSION" "$WORK/sp211native.pkg"

sed "s/__VERSION__/$VERSION/" packaging/distribution-install.xml > "$WORK/dist-i.xml"
S=$WORK/dmg; mkdir -p "$S"
productbuild --quiet --distribution "$WORK/dist-i.xml" --resources packaging/resources \
  --package-path "$WORK" "$S/Instalează driverul.pkg"
# uninstaller: small AppleScript app that runs uninstall.sh as administrator
U="$S/Dezinstalează driverul.app"
osacompile -o "$U" packaging/uninstall.applescript
install -m 755 packaging/uninstall.sh "$U/Contents/Resources/uninstall.sh"
codesign --force --deep --sign - "$U" 2>/dev/null
# router setup app: installs/removes the AirPrint server on an OpenWrt router over SSH
A="$S/AirPrint pe router.app"
osacompile -o "$A" packaging/router-app.applescript
install -m 755 packaging/router-run.sh packaging/router-askpass.sh "$A/Contents/Resources/"
install -m 644 build/router/sp211-airprint-router.tar.gz "$A/Contents/Resources/"
codesign --force --deep --sign - "$A" 2>/dev/null

cp packaging/Citeste-ma.txt "$S/Citește-mă.txt"

mkdir -p "$OUT"; DMG="$OUT/RICOH-SP211-Native-$VERSION.dmg"; rm -f "$DMG"
hdiutil create -quiet -volname "Driver Ricoh SP 211" -srcfolder "$S" -fs HFS+ -format UDZO "$DMG"
echo "Built $DMG"
