#!/usr/bin/env python3
"""
Post-process C/C++ files to vertically align #define macros.

Rules:
- Groups consecutive #define lines into a block.
- Comments and blank lines are allowed inside a block.
- Each block is aligned independently.
- Non-macro code is untouched.
"""

import re
import sys
from pathlib import Path

# Match: "<indent>#define MACRO(...whatever...)   replacement"
MACRO_RE = re.compile(
    r'^(\s*#\s*define\s+[A-Za-z_][A-Za-z0-9_]*(?:\([^)]*\))?)(\s+)(.*)$'
)

def align_macros_in_text(text: str) -> str:
    lines = text.splitlines(keepends=True)
    n = len(lines)
    i = 0

    while i < n:
        # Start of a macro block
        if not MACRO_RE.match(lines[i]):
            i += 1
            continue

        group_indices = []
        j = i

        # Collect block: allow comments and blank lines between macros
        while j < n:
            line = lines[j]
            if MACRO_RE.match(line):
                group_indices.append(j)
            else:
                stripped = line.strip()
                # Comments or empty lines are allowed inside the macro block
                if stripped == "" or stripped.startswith("/*") or stripped.startswith("//"):
                    pass
                else:
                    # Any "real" code (not macro/comment/blank) ends the block
                    break
            j += 1

        # Align only if there are >= 2 macros in the block
        if len(group_indices) >= 2:
            lefts = {}
            rights = {}
            maxlen = 0

            # Pass 1: compute max left length
            for idx in group_indices:
                m = MACRO_RE.match(lines[idx])
                if not m:
                    continue
                left, spaces, right = m.groups()
                right = right.rstrip("\n")
                lefts[idx] = left
                rights[idx] = right.strip()
                if len(left) > maxlen:
                    maxlen = len(left)

            # Pass 2: rewrite aligned macros
            for idx in group_indices:
                left = lefts[idx]
                right = rights[idx]
                if right:
                    lines[idx] = f"{left.ljust(maxlen)}   {right}\n"
                else:
                    lines[idx] = left + "\n"

        i = j

    return "".join(lines)

def main(path: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    new_text = align_macros_in_text(text)

    # Only write on change (an extra safety)
    if new_text != text:
        p.write_text(new_text, encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: macro_align.py <file.c|file.h>")
        sys.exit(1)

    main(sys.argv[1])
