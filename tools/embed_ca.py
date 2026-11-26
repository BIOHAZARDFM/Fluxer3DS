#!/usr/bin/env python3
"""Convert the Mozilla CA PEM bundle into a C header for first-run SD installation."""
from pathlib import Path
import sys

source = Path(sys.argv[1])
target = Path(sys.argv[2])
pem = source.read_text(encoding="ascii")
if "-----BEGIN CERTIFICATE-----" not in pem or "-----END CERTIFICATE-----" not in pem:
    raise SystemExit("Invalid CA bundle: no PEM certificate blocks found")
lines = ['/* Generated from data/ssl/cacert.pem; do not edit by hand. */',
         'static const char fluxer_ca_bundle[] =']
for start in range(0, len(pem), 88):
    chunk = pem[start:start + 88]
    escaped = chunk.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    lines.append('    "' + escaped + '"')
lines[-1] += ';'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text("\n".join(lines) + "\n", encoding="ascii")
print(f"Embedded {pem.count('-----BEGIN CERTIFICATE-----')} CA certificates ({len(pem)} bytes)")
