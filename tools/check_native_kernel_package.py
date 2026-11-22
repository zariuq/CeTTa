#!/usr/bin/env python3
"""Verify the native kernel's content-addressed vendored source package."""
import argparse
import hashlib
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument("package",type=Path);p.add_argument("--components",type=Path);a=p.parse_args()
root=a.package.resolve();manifest=json.loads((root/"SOURCE.json").read_text())
if not manifest["files"]:raise SystemExit("empty native kernel package")
for name,digest in manifest["files"].items():
    path=(root/name).resolve()
    if not path.is_relative_to(root):raise SystemExit("native kernel source escapes package: "+name)
    if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise SystemExit("native kernel source mismatch: "+name)
print(f"PASS: {len(manifest['files'])} pinned native kernel source files")

if a.components:
    components=a.components.resolve();component_root=components.parent
    record=json.loads(components.read_text())
    if record.get("format")!="CettaKernelComponentSourcesV1" or not record.get("files"):
        raise SystemExit("invalid native component source record")
    for name,digest in record["files"].items():
        source=(component_root/name).resolve()
        if not source.is_relative_to(component_root):raise SystemExit("component source escapes package: "+name)
        if hashlib.sha256(source.read_bytes()).hexdigest()!=digest:
            raise SystemExit("native component source mismatch: "+name)
    print(f"PASS: {len(record['files'])} pinned matching/observation component inputs")
