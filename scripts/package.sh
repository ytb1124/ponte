#!/bin/bash
# Build an installer package for macOS: signed and optionally notarized when
# Developer ID identities exist, otherwise unsigned (ad-hoc signed binaries).
#
#   scripts/package.sh              build + sign  -> dist/Ponte-<version>.pkg
#   scripts/package.sh --notarize   ... then notarize with Apple and staple
#
# Needs a "Developer ID Application" and a "Developer ID Installer" identity in
# the keychain. Notarization reads its credentials from a keychain profile:
#   xcrun notarytool store-credentials ponte-notary --apple-id <id> --team-id <team>
#
# Environment overrides: SIGN_APP, SIGN_PKG (identity name or SHA-1),
# NOTARY_PROFILE, VERSION, ARCHS, CMAKE.
set -euo pipefail
cd "$(dirname "$0")/.."
# keep AppleDouble (._*) files of extended attributes out of the payload
export COPYFILE_DISABLE=1

NOTARIZE=0
[ "${1:-}" = "--notarize" ] && NOTARIZE=1

CMAKE="${CMAKE:-$(command -v cmake || echo /opt/homebrew/bin/cmake)}"
ARCHS="${ARCHS:-arm64;x86_64}"
NOTARY_PROFILE="${NOTARY_PROFILE:-ponte-notary}"
VERSION="${VERSION:-$(sed -n 's/^project(.* VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)}"

# pick identities by hash: a certificate imported twice makes the name ambiguous
find_identity() {
    security find-identity -v ${2:+-p "$2"} | grep "\"$1" | head -1 | awk '{print $2}' || true
}
SIGN_APP="${SIGN_APP:-$(find_identity "Developer ID Application" codesigning)}"
SIGN_PKG="${SIGN_PKG:-$(find_identity "Developer ID Installer")}"
# without Developer ID identities: ad-hoc signed binaries in an unsigned
# package. It installs fine, but Gatekeeper asks the user to allow it once
# (System Settings > Privacy & Security > Open Anyway).
UNSIGNED=0
if [ -z "$SIGN_APP" ] || [ -z "$SIGN_PKG" ]; then
    echo "no Developer ID identities in the keychain - building an UNSIGNED package (ad-hoc signed binaries)" >&2
    [ "$NOTARIZE" = 1 ] && { echo "--notarize needs Developer ID identities" >&2; exit 1; }
    UNSIGNED=1
    SIGN_APP="-"
fi
SIGN_FLAGS=(--options runtime --timestamp)
[ "$UNSIGNED" = 1 ] && SIGN_FLAGS=()

BUILD=build-pkg
# stage outside the project: in a synced folder (iCloud Documents) the file
# provider keeps re-tagging the .app with FinderInfo, which codesign rejects
STAGE="$(mktemp -d "${TMPDIR:-/tmp}/ponte-pkg.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT
ROOT="$STAGE/root"
PKG="dist/Ponte-${VERSION}.pkg"

echo "== build ${VERSION} (${ARCHS}) =="
"$CMAKE" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=${ARCHS}" >/dev/null
"$CMAKE" --build "$BUILD" -j"$(sysctl -n hw.ncpu)"

echo "== assemble app bundle =="
if [ ! -f "$BUILD/icons/AppIcon.icns" ]; then
    mkdir -p "$BUILD/icons/AppIcon.iconset"
    swift scripts/render_icons.swift "$BUILD/icons"
    for sz in 16 32 64 128 256 512; do
        sips -z $sz $sz "$BUILD/icons/AppIcon-1024.png" --out "$BUILD/icons/AppIcon.iconset/icon_${sz}x${sz}.png" >/dev/null
        sips -z $((sz*2)) $((sz*2)) "$BUILD/icons/AppIcon-1024.png" --out "$BUILD/icons/AppIcon.iconset/icon_${sz}x${sz}@2x.png" >/dev/null
    done
    iconutil -c icns "$BUILD/icons/AppIcon.iconset" -o "$BUILD/icons/AppIcon.icns"
fi
APP="$ROOT/Applications/Ponte.app"
DRIVER="$ROOT/Library/Audio/Plug-Ins/HAL/Ponte.driver"
SHARE="$ROOT/usr/local/share/ponte"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" "$(dirname "$DRIVER")" "$SHARE" dist
cp launchd/DaemonApp-Info.plist "$APP/Contents/Info.plist"
plutil -replace CFBundleShortVersionString -string "$VERSION" "$APP/Contents/Info.plist"
plutil -replace CFBundleVersion -string "$VERSION" "$APP/Contents/Info.plist"
cp "$BUILD/ponte-daemon" "$APP/Contents/MacOS/"
cp "$BUILD/icons/AppIcon.icns" "$BUILD/icons/MenuIcon.png" "$BUILD/icons/MenuIcon@2x.png" "$APP/Contents/Resources/"
cp -R "$BUILD/Ponte.driver" "$DRIVER"
cp -R webui "$SHARE/webui"
cp launchd/io.github.ytb1124.ponte.daemon.plist scripts/uninstall.sh "$SHARE/"
xattr -cr "$ROOT"

echo "== sign (hardened runtime, timestamped) =="
codesign --force ${SIGN_FLAGS[@]+"${SIGN_FLAGS[@]}"} -s "$SIGN_APP" "$DRIVER"
codesign --force ${SIGN_FLAGS[@]+"${SIGN_FLAGS[@]}"} -s "$SIGN_APP" --identifier io.github.ytb1124.ponte.daemon "$APP"
codesign --verify --strict --verbose=2 "$DRIVER" "$APP"

echo "== package =="
# without this the installer "relocates" the app onto any other copy of the
# same bundle id it finds on the disk (e.g. a developer's build folder)
pkgbuild --analyze --root "$ROOT" "$STAGE/components.plist" >/dev/null
COUNT="$(grep -c "RootRelativeBundlePath" "$STAGE/components.plist")"
for ((i = 0; i < COUNT; i++)); do
    plutil -replace "${i}.BundleIsRelocatable" -bool false "$STAGE/components.plist"
    plutil -replace "${i}.BundleIsVersionChecked" -bool false "$STAGE/components.plist"
done
pkgbuild --root "$ROOT" --component-plist "$STAGE/components.plist" --scripts scripts/pkg \
         --identifier io.github.ytb1124.ponte.pkg --version "$VERSION" --install-location / \
         --ownership recommended "$STAGE/ponte-component.pkg" >/dev/null
productbuild --synthesize --package "$STAGE/ponte-component.pkg" "$STAGE/distribution.xml"
sed -i '' "s|<installer-gui-script\(.*\)>|<installer-gui-script\1><title>Ponte ${VERSION}</title><license file=\"LICENSE\"/>|" \
    "$STAGE/distribution.xml"
mkdir -p "$STAGE/resources"
cp LICENSE "$STAGE/resources/LICENSE"
PKG_SIGN=(--sign "$SIGN_PKG" --timestamp)
[ "$UNSIGNED" = 1 ] && PKG_SIGN=()
productbuild --distribution "$STAGE/distribution.xml" --package-path "$STAGE" \
             --resources "$STAGE/resources" ${PKG_SIGN[@]+"${PKG_SIGN[@]}"} "$PKG" >/dev/null
pkgutil --check-signature "$PKG" | sed -n '1,6p' || true

if [ "$NOTARIZE" = 1 ]; then
    echo "== notarize (uploads the package to Apple, usually a few minutes) =="
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
    spctl --assess --type install -vv "$PKG"
elif [ "$UNSIGNED" = 0 ]; then
    echo "not notarized - run 'scripts/package.sh --notarize' for a package other Macs accept"
fi
echo "done: $PKG"
