#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TARGET_SCRIPT="$SCRIPT_DIR/EngineEditScript.sh"

if [[ ! -f "$TARGET_SCRIPT" ]]; then
    echo "[ERROR] Target script not found: $TARGET_SCRIPT"
    echo ""
    read -n 1 -s -r -p "Press any key to close..."
    echo ""
    exit 1
fi

bash "$TARGET_SCRIPT"
EXIT_CODE=$?

echo ""
read -n 1 -s -r -p "Press any key to close..."
echo ""

exit $EXIT_CODE
