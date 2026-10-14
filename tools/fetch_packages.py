#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""Fetch the released packages the web installer offers (web/releases.json) for a local build of the site.

  python3 tools/fetch_packages.py [DIR]        (default: build/scratch/packages)

Each choralroot-X.Y.fwsc is downloaded from its GitHub release (the tag in releases.json) unless a file with the
right sha256 is already there, and checked against the sha256 in releases.json: a file that does not match is
removed and the script fails. The download is Python's own (urllib); when that fails (on a Mac whose Python has
no CA certificates it does: CERTIFICATE_VERIFY_FAILED) the script says so and fetches the same URL with `curl -sL`,
which uses the system's certificates. Either way the file is kept only when its sha256 is the listed one, and each
line says which way the file came. web/make_site.py and tests/site_test.py take the folder by default (or
CR_PACKAGES_DIR / --packages); without it a local site offers only the version it was built for.
Needs the network; nothing else in the build does. The Pages job does the same with `gh release download`.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def download(url):
    """-> (the bytes or None, how: what was used, and why when something failed)"""
    try:
        with urllib.request.urlopen(url, timeout=60) as f:
            return f.read(), "downloaded with urllib"
    except OSError as e:
        why = f"urllib failed ({getattr(e, 'reason', e)})"
    curl = shutil.which("curl")
    if not curl:
        return None, f"{why}; no curl to fall back to"
    try:
        r = subprocess.run([curl, "-sL", "--fail", "--max-time", "120", url], capture_output=True, timeout=150)
    except (OSError, subprocess.TimeoutExpired) as e:
        return None, f"{why}; curl -sL failed too ({e})"
    if r.returncode != 0 or not r.stdout:
        return None, f"{why}; curl -sL failed too (exit {r.returncode}{', ' + r.stderr.decode(errors='replace').strip() if r.stderr else ''})"
    return r.stdout, f"{why}; downloaded with curl -sL"


def main(out):
    data = json.loads((ROOT / "web" / "releases.json").read_text(encoding="utf-8"))
    out.mkdir(parents=True, exist_ok=True)
    failed = 0
    for r in data["releases"]:
        name = f"choralroot-{r['version']}.fwsc"
        dst = out / name
        if dst.is_file() and hashlib.sha256(dst.read_bytes()).hexdigest() == r["sha256"]:
            print(f"{name}: there, sha256 ok")
            continue
        url = f"{data['repo']}/releases/download/{r['tag']}/{name}"
        raw, how = download(url)
        if raw is None:
            print(f"{name}: FAILED to download {url}: {how}")
            failed += 1
            continue
        got = hashlib.sha256(raw).hexdigest()
        if got != r["sha256"]:
            print(f"{name}: FAILED: {how}, but its sha256 is {got} and web/releases.json says {r['sha256']} (not kept)")
            if dst.is_file():
                dst.unlink()
            failed += 1
            continue
        dst.write_bytes(raw)
        print(f"{name}: {how}, {len(raw)} B, sha256 ok")
    print(f"{out}: " + (f"{failed} of {len(data['releases'])} package(s) FAILED" if failed
                        else f"all {len(data['releases'])} packages there, each with the sha256 of web/releases.json"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "scratch" / "packages"))
