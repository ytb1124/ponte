#!/bin/bash
# Remove driver, daemon and launch agent. coreaudiod restarts (audio blips).
set -uo pipefail

launchctl bootout "gui/$(id -u)/io.github.ytb1124.ponte.daemon" 2>/dev/null
rm -f "$HOME/Library/LaunchAgents/io.github.ytb1124.ponte.daemon.plist"

sudo rm -rf /Library/Audio/Plug-Ins/HAL/Ponte.driver
sudo rm -rf /Applications/Ponte.app
sudo rm -rf /usr/local/share/ponte
sudo killall coreaudiod

echo "removed. config/logs kept in ~/Library/Application Support/Ponte and ~/Library/Logs/Ponte"
