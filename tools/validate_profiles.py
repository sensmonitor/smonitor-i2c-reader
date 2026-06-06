#!/usr/bin/env python3

import json
import pathlib
import sys


def main() -> int:
    root = pathlib.Path(__file__).resolve().parents[1]
    failures = 0

    for path in sorted((root / "profiles").glob("*.json")):
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
            assert document.get("schema_version") == 1
            assert isinstance(document.get("bus"), dict)
            assert isinstance(document.get("devices"), list)
            assert document["devices"]
            print(f"OK  {path.relative_to(root)}")
        except (OSError, ValueError, AssertionError) as error:
            failures += 1
            print(f"ERR {path.relative_to(root)}: {error}", file=sys.stderr)

    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
