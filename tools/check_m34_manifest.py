#!/usr/bin/env python3
"""Validate the temporary M34 roadmap inventory without third-party dependencies."""

import json
import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "m34_feature_manifest.json"


def fail(message: str) -> None:
    print(f"m34 manifest: error: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    try:
        data = json.loads(MANIFEST.read_text())
    except (OSError, json.JSONDecodeError) as exc:
        fail(str(exc))

    if data.get("schema_version") != 1:
        fail("unsupported schema_version")
    if data.get("roadmap") != "ROADMAP_M34_ACTIVE.md":
        fail("roadmap must point to ROADMAP_M34_ACTIVE.md")

    targets = data.get("targets", {})
    if targets.get("active") != ["arm64-darwin", "x86_64-b1nix"]:
        fail("active targets must be arm64-darwin and x86_64-b1nix")
    if not data.get("contracts", {}).get("hosted") or not data.get("contracts", {}).get("freestanding"):
        fail("hosted and freestanding contracts are required")

    features = data.get("features")
    if not isinstance(features, list) or not features:
        fail("features must be a non-empty list")

    valid_statuses = {"supported", "partial", "unsupported", "implementation_defined", "not_applicable"}
    valid_components = {"lexer", "preprocessor", "parser", "ast", "type_system", "ir", "backend", "diagnostics", "driver", "linker", "builtin_headers"}
    seen = set()
    for feature in features:
        required = {"id", "clause", "area", "status", "components", "tests"}
        missing = required - feature.keys()
        if missing:
            fail(f"{feature.get('id', '<unnamed>')} missing {', '.join(sorted(missing))}")
        if feature["id"] in seen:
            fail(f"duplicate feature id {feature['id']}")
        seen.add(feature["id"])
        if feature["status"] not in valid_statuses:
            fail(f"{feature['id']} has invalid status {feature['status']}")
        unknown = set(feature["components"]) - valid_components
        if unknown:
            fail(f"{feature['id']} has unknown components {sorted(unknown)}")
        if not feature["tests"]:
            fail(f"{feature['id']} has no named gate")
        for test in feature["tests"]:
            if not (ROOT / test).exists():
                fail(f"{feature['id']} references missing gate {test}")

    areas = {feature["area"] for feature in features}
    expected = {"M34"}
    if areas != expected:
        fail(f"coverage areas differ: expected {sorted(expected)}, got {sorted(areas)}")

    print(f"ok m34_manifest ({len(features)} feature records, {len(seen)} unique gates)")


if __name__ == "__main__":
    main()
