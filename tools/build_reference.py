"""Rebuild the pinned NTSC reference from a private user ROM and verify identity."""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys

from reference_index import COMMIT, generate

SHA256 = "12b77c4bc9c1832cee8881244659065ee1d84c70c3d29e6eaf92e6798cc2ca72"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    reference = root / "reference/sm-disassembly"
    revision = subprocess.check_output(["git", "-C", str(reference), "rev-parse", "HEAD"], text=True).strip()
    if revision != COMMIT:
        raise ValueError("Reference submodule does not match the pinned revision")
    data = args.rom.read_bytes()
    if len(data) == 0x300200:
        data = data[512:]
    if len(data) != 0x300000 or hashlib.sha256(data).hexdigest() != SHA256:
        raise ValueError("Expected the original NTSC ROM (an optional 512-byte header is supported)")
    build = root / "build"
    build.mkdir(exist_ok=True)
    normalized = build / "reference-input.sfc"
    normalized.write_bytes(data)
    with (build / "reference-extraction.log").open("w") as log:
        subprocess.run([sys.executable, str(reference / "tools/rip_assets.py"), str(normalized),
                        "-o", str(reference / "data")], stdout=log, stderr=subprocess.STDOUT, check=True)
    output, symbols = build / "reference.ntsc.sfc", build / "reference.sym"
    output.write_bytes(b"\xFF" * 0x300000)
    asar = reference / "tools" / ("asar.exe" if os.name == "nt" else "asar-standalone")
    if os.name != "nt":
        asar.chmod(asar.stat().st_mode | 0o111)
    subprocess.run([str(asar), "--no-title-check", "--symbols=wla", f"--symbols-path={symbols}",
                    str(reference / "src/main.asm"), str(output)], check=True)
    if output.read_bytes() != data:
        raise ValueError("Reference rebuild differs from the user ROM")
    generate(symbols, root / "native/generated/reference_index.hpp")
    print(f"Byte-exact reference verified: {SHA256}")
    print(f"Private outputs: {build}")


if __name__ == "__main__":
    main()
