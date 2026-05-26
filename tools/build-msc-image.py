#!/usr/bin/env python3
"""
build-msc-image.py — bake msc-disk-files/ into a FAT12 image embedded in the
firmware as a const uint8_t array.

Strategy (mirrors slidecue/tools/build-msc-image.py):
  1. mtools (mformat + mcopy) is preferred — cross-platform, no root.
  2. macOS fallback: hdiutil.
  3. Otherwise: bail and ask user to install mtools.

Outputs:
  firmware/src/msc_disk_image.h   — const array consumed by msc_disk.cpp
  firmware/src/msc_disk_image.bin — raw FAT12 image (handy for debugging)
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = ROOT / "msc-disk-files"
# Pure-IDF main lives under firmware-idf-pure/main/; the generated header is
# what msc_disk.c includes, the .bin is kept for debugging convenience.
OUT_HEADER = ROOT / "firmware-idf-pure" / "main" / "msc_disk_image.h"
OUT_BIN    = ROOT / "firmware-idf-pure" / "main" / "msc_disk_image.bin"

DISK_SIZE     = 512 * 1024          # 512 KB — plenty for README + JSON
VOLUME_LABEL  = "OmniRemote"

SKIP_NAMES = {".DS_Store", "__MACOSX", "__pycache__", "node_modules"}


def has(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def stage_payload(payload_dir: Path):
    """Copy msc-disk-files/* into the payload dir, skipping junk."""
    for entry in sorted(SRC_DIR.iterdir()):
        if entry.name in SKIP_NAMES or entry.name.startswith("."):
            continue
        dst = payload_dir / entry.name
        if entry.is_dir():
            shutil.copytree(entry, dst, dirs_exist_ok=True)
        else:
            shutil.copyfile(entry, dst)


def build_with_mtools(image_path: Path, payload_dir: Path):
    print("[+] Using mtools (superfloppy layout: FAT12 directly at LBA 0)")
    # Pre-fill 1 image-sized zero file; mformat writes FAT12 boot sector at
    # offset 0 (no MBR / no partition table). This matches what Arduino's
    # USBMSC class expects when exposing a single-LUN virtual drive.
    image_path.write_bytes(b"\x00" * DISK_SIZE)
    total_sectors = DISK_SIZE // 512

    subprocess.run([
        "mformat", "-i", str(image_path),
        "-v", VOLUME_LABEL,
        "-N", "0DECAF00",
        "-T", str(total_sectors),
        "-h", "2", "-s", "32",
        "-c", "1",
        "-r", "224",
        "::"
    ], check=True)

    for f in sorted(payload_dir.iterdir()):
        subprocess.run([
            "mcopy", "-s", "-i", str(image_path), "-o", str(f), "::"
        ], check=True)


def build_with_hdiutil(image_path: Path, payload_dir: Path):
    print("[+] Using hdiutil")
    with tempfile.TemporaryDirectory() as tmp:
        dmg = Path(tmp) / "build.dmg"
        subprocess.run([
            "hdiutil", "create",
            "-size", f"{DISK_SIZE // 1024}k",
            "-fs", "MS-DOS FAT12",
            "-volname", VOLUME_LABEL,
            "-layout", "MBRSPUD",
            "-ov",
            str(dmg)
        ], check=True, capture_output=True)
        mnt = Path(tmp) / "mnt"
        subprocess.run(["hdiutil", "attach", "-nobrowse",
                        "-mountpoint", str(mnt), str(dmg)],
                       check=True, capture_output=True)
        try:
            for f in sorted(payload_dir.iterdir()):
                if f.is_dir():
                    shutil.copytree(f, mnt / f.name, dirs_exist_ok=True)
                else:
                    shutil.copyfile(f, mnt / f.name)
            for ad in mnt.rglob("._*"):
                ad.unlink()
        finally:
            subprocess.run(["hdiutil", "detach", str(mnt)],
                           check=True, capture_output=True)
        shutil.copy(dmg, image_path)


def patch_fat_volume_label(image_path: Path):
    """Force the BPB + root-dir volume label to mixed case so Finder shows
    'OmniRemote' instead of 'OMNIREMOTE'. Lifted from slidecue/tools/."""
    data = bytearray(image_path.read_bytes())
    label = VOLUME_LABEL.encode("ascii").ljust(11, b" ")[:11]

    # Superfloppy layout: filesystem starts at offset 0. (If we ever switch
    # back to MBR + partition, walk LBA from the partition table here.)
    base = 0
    if data[0x1FE:0x200] == b"\x55\xAA" and data[0x1C2] != 0:
        part_lba = int.from_bytes(data[0x1C6:0x1CA], "little")
        base = part_lba * 512

    # 1) BPB extended volume label (the field most tools read first).
    data[base + 0x2B:base + 0x36] = label

    # 2) Root-directory volume-label entry (attribute 0x08) — Finder reads
    #    this on macOS.
    bpb = data[base:base + 64]
    bytes_per_sector = int.from_bytes(bpb[11:13], "little")
    reserved         = int.from_bytes(bpb[14:16], "little")
    fat_count        = bpb[16]
    root_entries     = int.from_bytes(bpb[17:19], "little")
    sectors_per_fat  = int.from_bytes(bpb[22:24], "little")
    if bytes_per_sector and root_entries:
        root_start = base + (reserved + fat_count * sectors_per_fat) * bytes_per_sector
        root_size  = root_entries * 32
        for off in range(root_start, root_start + root_size, 32):
            first = data[off]
            if first == 0x00: break
            if first == 0xE5: continue
            if data[off + 11] == 0x08:   # ATTR_VOLUME_ID
                data[off:off + 11] = label
                break

    image_path.write_bytes(data)


def emit_header(image_path: Path):
    data = image_path.read_bytes()
    if len(data) < DISK_SIZE:
        data += b"\x00" * (DISK_SIZE - len(data))
    elif len(data) > DISK_SIZE:
        data = data[:DISK_SIZE]
    image_path.write_bytes(data)

    lines = [
        "/* msc_disk_image.h — generated by tools/build-msc-image.py */",
        "/* DO NOT EDIT BY HAND */",
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "static const uint8_t MSC_DISK_IMAGE[] __attribute__((aligned(4))) = {",
    ]
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        hexed = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hexed},")
    lines.append("};")
    lines.append(f"static const size_t MSC_DISK_IMAGE_SIZE = {len(data)};")
    lines.append(f"static const uint32_t MSC_DISK_BLOCK_SIZE  = 512;")
    lines.append(f"static const uint32_t MSC_DISK_BLOCK_COUNT = {len(data) // 512};")
    lines.append("")
    OUT_HEADER.write_text("\n".join(lines))
    print(f"[ok] image  {len(data)} bytes -> {image_path.name}")
    print(f"[ok] header {OUT_HEADER}")


def main():
    OUT_BIN.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        payload = Path(tmp) / "payload"
        payload.mkdir()
        stage_payload(payload)

        files = sorted(p for p in payload.rglob("*") if p.is_file())
        total = sum(f.stat().st_size for f in files)
        print(f"[+] payload: {len(files)} files, {total} bytes")
        for f in files:
            print(f"    {f.relative_to(payload)}  ({f.stat().st_size} B)")

        if has("mformat") and has("mcopy"):
            build_with_mtools(OUT_BIN, payload)
        elif sys.platform == "darwin" and has("hdiutil"):
            build_with_hdiutil(OUT_BIN, payload)
        else:
            sys.exit("No FAT12 builder. brew install mtools  (or apt install mtools)")

    patch_fat_volume_label(OUT_BIN)
    emit_header(OUT_BIN)


if __name__ == "__main__":
    main()
