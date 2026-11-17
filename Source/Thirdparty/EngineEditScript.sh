#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PLUGIN_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
ENGINE_DIR="$(cd "$PLUGIN_DIR/../.." && pwd)"
EDITED_DIR="$SCRIPT_DIR/EngineEdit"
RESOLVED_EDITED_FILE=""
RESOLVED_TARGET_FILE=""

declare -a MODIFIED_FILES=()
declare -a ADDED_FILES=()

log() {
    echo "[INFO] $1"
}

warn() {
    echo "[WARN] $1"
}

err() {
    echo "[ERROR] $1" >&2
}

add_modified_file() {
    local file="$1"
    for existing in "${MODIFIED_FILES[@]:-}"; do
        if [[ "$existing" == "$file" ]]; then
            return
        fi
    done
    MODIFIED_FILES+=("$file")
}

add_added_file() {
    local file="$1"
    for existing in "${ADDED_FILES[@]:-}"; do
        if [[ "$existing" == "$file" ]]; then
            return
        fi
    done
    ADDED_FILES+=("$file")
}

confirm_proceed() {
    echo "### Update Engine sourceCode For MetalFX ###"
    echo "# Additional changes may be required depending on each user's environment."
    echo "# Do you want to continue? [y/N]"
    read -r ANSWER

    case "$ANSWER" in
        y|Y|yes|Yes|YES)
            echo "Starting update process..."
            return 0
            ;;
        *)
            echo "Operation cancelled."
            exit 0
            ;;
    esac
}

validate_plugin_dir() {
    local plugin_descriptor_count
    plugin_descriptor_count=$(find "$PLUGIN_DIR" -maxdepth 1 -type f -name "*.uplugin" | wc -l | tr -d ' ')

    if [[ "$plugin_descriptor_count" -eq 0 ]]; then
        err "No .uplugin file was found in the plugin directory: $PLUGIN_DIR"
        err "Please make sure this script is placed under a valid Unreal Engine plugin."
        exit 1
    fi

    if [[ "$plugin_descriptor_count" -gt 1 ]]; then
        err "Multiple .uplugin files were found in the plugin directory: $PLUGIN_DIR"
        err "The plugin directory is ambiguous."
        exit 1
    fi

    if [[ ! -d "$PLUGIN_DIR/Source" ]]; then
        err "Plugin Source directory not found: $PLUGIN_DIR/Source"
        err "Please make sure the plugin directory structure is valid."
        exit 1
    fi

    local normalized_plugin_dir
    local normalized_engine_plugins_dir
    normalized_plugin_dir="$(cd "$PLUGIN_DIR" && pwd)"
    normalized_engine_plugins_dir="$(cd "$ENGINE_DIR/Plugins" && pwd)"

    case "$normalized_plugin_dir" in
        "$normalized_engine_plugins_dir"/*)
            ;;
        *)
            err "Plugin directory is not located under Engine/Plugins."
            err "Plugin directory : $normalized_plugin_dir"
            err "Expected prefix   : $normalized_engine_plugins_dir"
            exit 1
            ;;
    esac
}

validate_engine_version() {
    local build_version_file="$ENGINE_DIR/Build/Build.version"

    if [[ ! -f "$build_version_file" ]]; then
        err "Engine version file not found: $build_version_file"
        err "Please make sure the engine directory is valid."
        exit 1
    fi

    local version_output
    version_output=$(python3 - "$build_version_file" <<'PY'
import json
import sys

path = sys.argv[1]
with open(path, "r", encoding="utf-8") as f:
    data = json.load(f)

major = data.get("MajorVersion", "")
minor = data.get("MinorVersion", "")
patch = data.get("PatchVersion", "")

print(major)
print(f"{major}.{minor}.{patch}")
PY
)

    local major_version
    local full_version
    major_version="$(printf '%s\n' "$version_output" | sed -n '1p')"
    full_version="$(printf '%s\n' "$version_output" | sed -n '2p')"

    if [[ -z "$major_version" ]]; then
        err "Failed to read MajorVersion from: $build_version_file"
        exit 1
    fi

    if ! [[ "$major_version" =~ ^[0-9]+$ ]]; then
        err "Invalid MajorVersion value: $major_version"
        exit 1
    fi

    if (( major_version < 5 )); then
        err "Unreal Engine 5.0 or later is required. Current version: $full_version"
        exit 1
    fi

    log "Detected Unreal Engine version: $full_version"
}

require_paths() {
    if [[ ! -d "$PLUGIN_DIR" ]]; then
        err "Plugin directory not found: $PLUGIN_DIR"
        exit 1
    fi

    if [[ ! -d "$ENGINE_DIR" ]]; then
        err "Engine directory not found: $ENGINE_DIR"
        exit 1
    fi

    if [[ ! -d "$ENGINE_DIR/Plugins" ]]; then
        err "Engine Plugins directory not found: $ENGINE_DIR/Plugins"
        exit 1
    fi

    if [[ ! -d "$EDITED_DIR" ]]; then
        err "Edited directory not found: $EDITED_DIR"
        exit 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        err "python3 is required but was not found."
        exit 1
    fi

    validate_plugin_dir
    validate_engine_version
}

resolve_source_for_new_file() {
    local relative_path="$1"
    local edited_file="$EDITED_DIR/$relative_path"

    if [[ ! -f "$edited_file" ]]; then
        err "Edited file not found: $edited_file"
        return 1
    fi

    RESOLVED_EDITED_FILE="$edited_file"
    RESOLVED_TARGET_FILE="$ENGINE_DIR/$relative_path"
}

add_file_if_missing() {
    local relative_path="$1"

    resolve_source_for_new_file "$relative_path" || return 1

    local edited_file="$RESOLVED_EDITED_FILE"
    local target_file="$RESOLVED_TARGET_FILE"
    local target_dir
    target_dir="$(dirname "$target_file")"

    echo ""
    echo "----------------------------------------"
    echo "Adding or overwriting file"
    echo "Edited file : $edited_file"
    echo "Target file : $target_file"
    echo "----------------------------------------"

    if [[ ! -d "$target_dir" ]]; then
        mkdir -p "$target_dir"
    fi

    if [[ -f "$target_file" ]]; then
        warn "Target file already exists. It will be overwritten: $target_file"
        cp "$edited_file" "$target_file"
        add_modified_file "$target_file"
        echo "File overwrite completed."
        return 0
    fi

    cp "$edited_file" "$target_file"
    add_added_file "$target_file"

    echo "File add completed."
}

run_updates() {
    add_file_if_missing "Source/Runtime/Apple/MetalRHI/Public/MetalRHIUtility.h"
    add_file_if_missing "Source/Runtime/Apple/MetalRHI/Private/MetalRHIUtility.cpp"
}

print_modified_files() {
    echo ""
    echo "============================"
    echo "Modified files"
    echo "============================"

    if [[ ${#MODIFIED_FILES[@]} -eq 0 ]]; then
        echo "(none)"
    else
        printf '%s\n' "${MODIFIED_FILES[@]}"
    fi

    echo ""
    echo "============================"
    echo "Added files"
    echo "============================"

    if [[ ${#ADDED_FILES[@]} -eq 0 ]]; then
        echo "(none)"
    else
        printf '%s\n' "${ADDED_FILES[@]}"
    fi
}

main() {
    require_paths
    confirm_proceed
    run_updates
    print_modified_files
}

main "$@"
