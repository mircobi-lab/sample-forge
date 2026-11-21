#!/bin/bash
# Sample Forge · MBStudio91 — apre la pagina in una finestra dedicata (Chrome o simili), altrimenti nel browser predefinito
DIR="$(cd "$(dirname "$0")/../Resources" && pwd)"
URL="file://$(printf '%s' "$DIR/index.html" | sed -e 's/%/%25/g' -e 's/ /%20/g' -e 's/#/%23/g' -e 's/?/%3F/g')"
PROFILE="$HOME/Library/Application Support/SampleForge"
for B in "Google Chrome" "Microsoft Edge" "Brave Browser" "Chromium"; do
  for BASE in "/Applications" "$HOME/Applications"; do
    BIN="$BASE/$B.app/Contents/MacOS/$B"
    if [ -x "$BIN" ]; then
      mkdir -p "$PROFILE"
      exec "$BIN" --app="$URL" --user-data-dir="$PROFILE" --no-first-run --no-default-browser-check --window-size=1180,900 >/dev/null 2>&1
    fi
  done
done
exec /usr/bin/open "$DIR/index.html"
