#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "==> Building web UI..."
cd "$SCRIPT_DIR/thermometer-ui"
npm run build

echo "==> Building and flashing firmware + SPIFFS image..."
cd "$SCRIPT_DIR/firmware"
source "$IDF_PATH/export.sh" 2>/dev/null || true
idf.py build flash

echo "==> Done. Device is rebooting."
