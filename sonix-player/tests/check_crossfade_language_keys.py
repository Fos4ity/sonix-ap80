#!/usr/bin/env python3
"""Every crossfade string must exist in both players' language files.

The player shows the raw key when a translation is missing, so a new setting
that only English carries is a visible defect in every other language. This
gate keeps the crossfade keys in step across R1 and R3 Pro II.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TREES = [
    REPO / "sonix-packer/assets/R1/usr/resource/sonix/language",
    REPO / "sonix-packer/assets/R3PII/usr/resource/sonix/language",
]
KEYS = {
    "musicsettings_true_crossfade",
    "musicsettings_true_crossfade_note",
    "musicsettings_crossfade_duration",
}


def load(path: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        text = line.strip()
        if not text or text[0] in "#;[" or "=" not in text:
            continue
        key, value = text.split("=", 1)
        entries[key.strip()] = value.strip()
    return entries


def main() -> int:
    errors: list[str] = []
    for tree in TREES:
        files = sorted(tree.glob("*.ini"))
        if not files:
            errors.append(f"{tree}: no language files")
            continue
        for path in files:
            entries = load(path)
            for key in sorted(KEYS):
                if key not in entries:
                    errors.append(f"{path}: missing {key}")
                elif not entries[key]:
                    errors.append(f"{path}: empty {key}")
    if errors:
        for error in errors:
            print(error)
        print("FAILURES")
        return 1
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
