#!/usr/bin/env python3
"""Prepare pinned upstream Linux x86_64 binaries for a local release build."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import tempfile
import urllib.request
import zipfile

ASSETS = (
    ("yt-dlp_linux", "https://github.com/yt-dlp/yt-dlp/releases/download/2026.11.23/yt-dlp_linux", "249ddb058234276bc449b9cd9492aad9f133bda5923c61a9e39983ae114ef17d", 41184064),
    ("deno.zip", "https://github.com/denoland/deno/releases/download/v2.9.8/deno-x86_64-unknown-linux-gnu.zip", "e890975bc2549807bbcc0b584ffafda72cc7c93349dd99c80483dd784f6c4609", 41963064),
)

def verified(path, digest, size):
    if not path.is_file() or path.stat().st_size != size:
        return False
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest() == digest

def download(cache, asset):
    name, url, digest, size = asset
    path = cache / name
    if verified(path, digest, size):
        return path
    with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
        temp = Path(output.name)
        try:
            with urllib.request.urlopen(url, timeout=30) as response:
                if not response.geturl().startswith("https://"):
                    raise RuntimeError("HTTPS required")
                total = 0
                while chunk := response.read(1024 * 1024):
                    total += len(chunk)
                    if total > size:
                        raise RuntimeError("Unexpected download size")
                    output.write(chunk)
            output.flush()
            if not verified(temp, digest, size):
                raise RuntimeError("Checksum mismatch: " + name)
            os.replace(temp, path)
        finally:
            temp.unlink(missing_ok=True)
    return path

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        parser.error("This pinned bundle supports Linux x86_64 only")
    args.cache.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    yt, deno = [download(args.cache, asset) for asset in ASSETS]
    with tempfile.TemporaryDirectory(dir=args.output) as work:
        stage = Path(work)
        shutil.copyfile(yt, stage / "yt-dlp")
        with zipfile.ZipFile(deno) as archive, archive.open("deno") as source, (stage / "deno").open("wb") as target:
            if archive.getinfo("deno").file_size > 200 * 1024 * 1024:
                raise RuntimeError("Unexpected Deno executable size")
            shutil.copyfileobj(source, target)
        for name in ("yt-dlp", "deno"):
            (stage / name).chmod(0o755)
            os.replace(stage / name, args.output / name)
    (args.output / "RUNTIME-NOTICE.txt").write_text(
        "Pinned upstream executables for local testing.\n"
        "yt-dlp 2026.11.23: https://github.com/yt-dlp/yt-dlp\n"
        "Deno 2.9.8: https://github.com/denoland/deno\n"
        "Upstream source and license notices: see the corresponding tagged releases.\n"
        "Distribution requires review of bundled third-party licenses and source obligations.\n"
        + "\n".join(f"{url}\nSHA256 {digest}" for _, url, digest, _ in ASSETS) + "\n"
    )
    print("Verified runtime prepared:", args.output)

if __name__ == "__main__":
    main()
