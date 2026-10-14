#!/usr/bin/env python3
"""Keep PR #122's English placeholders out of completed Arabic translations."""

import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def main():
    script_dir = Path(__file__).resolve().parent
    catalogue = (Path(sys.argv[1]) if len(sys.argv) > 1 else
                 script_dir.parents[1] / "ui/translations/rockpool_ar.ts")
    # Exact context/source/comment identities from a2614e4, not a general ban
    # on Latin text: product names and other catalogues are outside this check.
    affected = {tuple(key) for key in json.loads(
        (script_dir / "arabic-placeholder-messages.json").read_text())}
    errors = []
    for context in ET.parse(catalogue).getroot().findall("context"):
        for message in context.findall("message"):
            source = message.findtext("source")
            key = (context.findtext("name"), source, message.findtext("comment"))
            if key not in affected:
                continue
            translation = message.find("translation")
            if translation is None:
                continue
            if translation.get("type") in {"unfinished", "vanished", "obsolete"}:
                continue
            target = "".join(translation.itertext()).strip()
            if not target or target == source.strip():
                errors.append(f"{key[0]}: {source}")
    for error in errors:
        print(f"{catalogue}: English placeholder marked complete: {error}",
              file=sys.stderr)
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
