#!/usr/bin/env python3
"""
Download the latest ROCKNIX RK3326 (-a variant for MagicX XU10) release from GitHub.
"""

import json
import sys
import urllib.request
from pathlib import Path


def main() -> None:
    out_dir = Path("backups")
    out_dir.mkdir(parents=True, exist_ok=True)

    api_url = "https://api.github.com/repos/ROCKNIX/distribution/releases/latest"
    req = urllib.request.Request(api_url, headers={"User-Agent": "XU10-Groovebox-Builder/1.0"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        release = json.loads(resp.read().decode("utf-8"))

    tag = release.get("tag_name", "unknown")
    print(f"Latest ROCKNIX release tag: {tag}")

    target_asset = None
    for asset in release.get("assets", []):
        name = asset.get("name", "")
        if "RK3326" in name and name.endswith(".img.gz"):
            print(f"  Found RK3326 asset: {name} ({asset.get('size', 0) / (1024*1024):.1f} MB)")
            if "-a.img.gz" in name or target_asset is None:
                target_asset = asset

    if not target_asset:
        print("ERROR: Could not find RK3326 .img.gz asset in latest release.", file=sys.stderr)
        sys.exit(1)

    asset_name = target_asset["name"]
    download_url = target_asset["browser_download_url"]
    expected_size = target_asset["size"]
    dest_path = out_dir / asset_name

    if dest_path.exists() and dest_path.stat().st_size == expected_size:
        print(f"Already downloaded and verified: {dest_path} ({expected_size} bytes)")
        return

    print(f"Downloading {download_url} -> {dest_path} ({expected_size / (1024*1024):.1f} MB)...")
    dl_req = urllib.request.Request(download_url, headers={"User-Agent": "XU10-Groovebox-Builder/1.0"})
    with urllib.request.urlopen(dl_req, timeout=60) as resp, open(dest_path, "wb") as f:
        downloaded = 0
        last_pct = -1
        while True:
            chunk = resp.read(1024 * 1024)
            if not chunk:
                break
            f.write(chunk)
            downloaded += len(chunk)
            pct = int(downloaded * 100 / expected_size) if expected_size else 0
            if pct >= last_pct + 10:
                print(f"  {pct}% ({downloaded / (1024*1024):.1f} / {expected_size / (1024*1024):.1f} MB)", flush=True)
                last_pct = pct

    actual_size = dest_path.stat().st_size
    if actual_size != expected_size:
        raise RuntimeError(f"Incomplete download: {actual_size} != {expected_size}")
    print(f"Successfully downloaded {dest_path} ({actual_size} bytes).")


if __name__ == "__main__":
    main()
