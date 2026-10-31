#!/bin/sh
# Adds (or with --remove, removes) a menu entry for this unpacked folder.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
apps=$data/applications
icons=$data/icons/hicolor/48x48/apps
entry=$apps/org.roadrash.nativeport.desktop
if [ "${1:-}" = --remove ]; then
    rm -f "$entry" "$icons/org.roadrash.nativeport.png"
    echo "removed $entry"
    exit 0
fi
mkdir -p "$apps" "$icons"
cp "$here/icon.png" "$icons/org.roadrash.nativeport.png"
# Desktop-entry quoting: " ` $ \ get a backslash, then the string escape
# doubles every backslash (Desktop Entry Specification, "Exec key").
RR_EXEC="\"$(printf '%s' "$here/rr-game" | sed -e 's/["`$\\]/\\&/g' -e 's/\\/\\\\/g')\"" \
awk 'BEGIN { e = ENVIRON["RR_EXEC"] }
     { i = index($0, "@EXEC@"); if (i) $0 = substr($0, 1, i - 1) e substr($0, i + 6)
       i = index($0, "@ICON@"); if (i) $0 = substr($0, 1, i - 1) "org.roadrash.nativeport" substr($0, i + 6)
       print }' "$here/road-rash-native.desktop.in" > "$entry"
if command -v update-desktop-database >/dev/null 2>&1; then update-desktop-database "$apps" >/dev/null 2>&1 || :; fi
echo "added $entry"
