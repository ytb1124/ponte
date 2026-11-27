#!/bin/bash
# Build + install the AES67 virtual soundcard: HAL driver, daemon, LaunchAgent.
# The coreaudiod restart interrupts ALL system audio for a moment.
set -euo pipefail
cd "$(dirname "$0")/.."

CMAKE="${CMAKE:-/opt/homebrew/bin/cmake}"

echo "== build =="
"$CMAKE" -B build -DCMAKE_BUILD_TYPE=Release
"$CMAKE" --build build -j"$(sysctl -n hw.ncpu)"

# sign with the local self-signed identity when present: its stable designated
# requirement keeps the "Local Network" TCC grant across updates. ad-hoc as
# fallback (grant then dies on every update).
IDENTITY="$(security find-identity -v -p codesigning 2>/dev/null | grep -o '"Ponte Signing"' | head -1 | tr -d '"' || true)"
SIGN_ID="${IDENTITY:--}"
echo "signing as: ${SIGN_ID}"
codesign --force -s "$SIGN_ID" build/Ponte.driver
codesign --force -s "$SIGN_ID" build/ponte-daemon
# quarantine xattrs (e.g. from sandboxed build tools) make gatekeeper refuse
# to load the ad-hoc signed plugin: "library load disallowed by system policy"
xattr -dr com.apple.quarantine build/Ponte.driver build/ponte-daemon 2>/dev/null || true

echo "== assemble daemon app bundle =="
# a proper .app with a stable bundle id keeps the "Local Network" permission
# across updates (an unbundled ad-hoc binary loses it on every new cdhash)
APP=build/Ponte.app
if [ ! -f build/icons/AppIcon.icns ]; then
    mkdir -p build/icons/AppIcon.iconset
    swift scripts/render_icons.swift build/icons
    for sz in 16 32 64 128 256 512; do
        sips -z $sz $sz build/icons/AppIcon-1024.png --out build/icons/AppIcon.iconset/icon_${sz}x${sz}.png >/dev/null
        sips -z $((sz*2)) $((sz*2)) build/icons/AppIcon-1024.png --out build/icons/AppIcon.iconset/icon_${sz}x${sz}@2x.png >/dev/null
    done
    iconutil -c icns build/icons/AppIcon.iconset -o build/icons/AppIcon.icns
fi
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp launchd/DaemonApp-Info.plist "$APP/Contents/Info.plist"
cp build/ponte-daemon "$APP/Contents/MacOS/"
cp build/icons/AppIcon.icns build/icons/MenuIcon.png build/icons/MenuIcon@2x.png "$APP/Contents/Resources/"
# copied resources may carry finder/provenance xattrs - codesign refuses the
# bundle with "resource fork ... detritus not allowed" unless they are gone
xattr -cr "$APP" 2>/dev/null || true
codesign --force -s "$SIGN_ID" --identifier io.github.ytb1124.ponte.daemon "$APP"
xattr -dr com.apple.quarantine "$APP" 2>/dev/null || true

echo "== install driver + daemon (sudo) =="
sudo rm -rf /Library/Audio/Plug-Ins/HAL/Ponte.driver
sudo cp -R build/Ponte.driver /Library/Audio/Plug-Ins/HAL/
sudo chown -R root:wheel /Library/Audio/Plug-Ins/HAL/Ponte.driver
sudo rm -rf /Applications/Ponte.app
sudo cp -R "$APP" /Applications/
sudo mkdir -p /usr/local/share/ponte
sudo rm -rf /usr/local/share/ponte/webui
sudo cp -R webui /usr/local/share/ponte/webui
sudo xattr -dr com.apple.quarantine /Library/Audio/Plug-Ins/HAL/Ponte.driver \
     /Applications/Ponte.app /usr/local/share/ponte 2>/dev/null || true

echo "== restart coreaudiod (system audio blips) =="
# SIP forbids kickstarting apple daemons - killall works, launchd restarts it
sudo killall coreaudiod
sleep 2

echo "== launch agent =="
mkdir -p "$HOME/Library/LaunchAgents" "$HOME/Library/Logs/Ponte" \
         "$HOME/Library/Application Support/Ponte"
sed "s|__HOME__|$HOME|g" launchd/io.github.ytb1124.ponte.daemon.plist \
    > "$HOME/Library/LaunchAgents/io.github.ytb1124.ponte.daemon.plist"
launchctl bootout "gui/$(id -u)/io.github.ytb1124.ponte.daemon" 2>/dev/null || true
# wait for the old instance to fully exit - bootstrap races it otherwise
for i in $(seq 1 15); do pgrep -f ponte-daemon >/dev/null || break; sleep 1; done
# a disabled agent (Login Items switched off) would fail to load with error 5
launchctl enable "gui/$(id -u)/io.github.ytb1124.ponte.daemon"
launchctl bootstrap "gui/$(id -u)" "$HOME/Library/LaunchAgents/io.github.ytb1124.ponte.daemon.plist"

sleep 2
echo "== verify =="
if system_profiler SPAudioDataType 2>/dev/null | grep -q "AES67"; then
    echo "OK: AES67 devices are visible"
else
    echo "WARNING: AES67 devices not (yet) visible - check 'log show --last 2m --predicate \"process == \\\"coreaudiod\\\"\"'"
fi
launchctl print "gui/$(id -u)/io.github.ytb1124.ponte.daemon" 2>/dev/null | grep -E "state|pid" | head -2 || true
echo "web ui:   http://localhost:8180"
echo "nmos api: http://<lan-ip>:8080/x-nmos/"
