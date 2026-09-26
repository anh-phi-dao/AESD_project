#!/bin/bash
set -euo pipefail

# Firmware root directory (parent of scripts/)
FIRMWARE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Select Python executable (prefer python for Windows/venv)
PYTHON=python
if ! command -v python >/dev/null 2>&1; then
    PYTHON=python3
fi

MACRO_ALIGN="$FIRMWARE_ROOT/scripts/macro_align.py"

run_on_file() {
    local f="$1"

    # Convert to absolute path for reliable comparison
    local abs_f
    abs_f="$(cd "$(dirname "$f")" 2>/dev/null && pwd)/$(basename "$f")"

    # Skip if file does not exist
    [ -f "$abs_f" ] || return 0

    # Process ONLY files located inside the Firmware directory
    case "$abs_f" in
        "$FIRMWARE_ROOT"/*) ;;
        *) return 0 ;;
    esac

    # Skip build output, third-party libraries, and vendor directories
    case "$abs_f" in
        */build/*|*/third_party/*|*/managed_components/*|*/.espressif/*|*/arm/*|*/.vscode/*|*/.git/*)
            return 0
            ;;
    esac

    case "$abs_f" in
        *.c|*.h|*.cpp|*.hpp) 
            echo "Formatting: $f"
            clang-format -i "$abs_f"
            "$PYTHON" "$MACRO_ALIGN" "$abs_f"
            ;;
        *)
            # Ignore non C/C++ files
            ;;
    esac
}

# If files are provided as arguments (e.g., from Git hook), format only those files
if [ "$#" -gt 0 ]; then
    for file in "$@"; do
        run_on_file "$file"
    done
else
    # If no arguments provided, format project C/C++ files inside Firmware/
    # (pruning build, third_party, and other non-project directories)
    cd "$FIRMWARE_ROOT"
    find . \( \
        -path './build' -o \
        -path './third_party' -o \
        -path './managed_components' -o \
        -path './.espressif' -o \
        -path './.vscode' -o \
        -path './arm' -o \
        -path './.git' \
    \) -prune -o -type f \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) -print | while read -r file; do
        run_on_file "$file"
    done
fi