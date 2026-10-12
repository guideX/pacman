#!/usr/bin/env python3
"""Wrap a verified guideXOS FAT32 ESP image in a protective GPT disk image."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import uuid
import zlib
from pathlib import Path


SECTOR = 512
PARTITION_START = 2048  # 1 MiB alignment.
GPT_ENTRY_COUNT = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_BYTES = GPT_ENTRY_COUNT * GPT_ENTRY_SIZE
GPT_ENTRY_SECTORS = GPT_ENTRY_BYTES // SECTOR
EFI_SYSTEM_PARTITION_GUID = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def make_partition_entry(partition_guid: uuid.UUID, first: int, last: int) -> bytes:
    entry = bytearray(GPT_ENTRY_SIZE)
    entry[0:16] = EFI_SYSTEM_PARTITION_GUID.bytes_le
    entry[16:32] = partition_guid.bytes_le
    struct.pack_into("<QQQ", entry, 32, first, last, 0)
    entry[56:128] = "guideXOS-PacMan-PST3".encode("utf-16le")
    return bytes(entry)


def make_gpt_header(
    *, current_lba: int, backup_lba: int, first_usable: int, last_usable: int,
    disk_guid: uuid.UUID, entries_lba: int, entries_crc: int,
) -> bytes:
    header = bytearray(SECTOR)
    header[0:8] = b"EFI PART"
    struct.pack_into("<II", header, 8, 0x00010000, 92)
    struct.pack_into("<I", header, 16, 0)
    struct.pack_into("<I", header, 20, 0)
    struct.pack_into("<QQQQ", header, 24, current_lba, backup_lba, first_usable, last_usable)
    header[56:72] = disk_guid.bytes_le
    struct.pack_into(
        "<QIII", header, 72, entries_lba, GPT_ENTRY_COUNT, GPT_ENTRY_SIZE, entries_crc
    )
    header_crc = zlib.crc32(header[:92]) & 0xFFFFFFFF
    struct.pack_into("<I", header, 16, header_crc)
    return bytes(header)


def make_protective_mbr(total_sectors: int) -> bytes:
    mbr = bytearray(SECTOR)
    offset = 446
    mbr[offset] = 0
    mbr[offset + 1 : offset + 4] = b"\x00\x02\x00"
    mbr[offset + 4] = 0xEE
    mbr[offset + 5 : offset + 8] = b"\xff\xff\xff"
    struct.pack_into("<II", mbr, offset + 8, 1, min(total_sectors - 1, 0xFFFFFFFF))
    mbr[510:512] = b"\x55\xaa"
    return bytes(mbr)


def create_image(source: Path, output: Path, report: Path) -> dict[str, object]:
    source = source.resolve(strict=True)
    output = output.resolve(strict=False)
    report = report.resolve(strict=False)
    if not source.is_file() or source.is_symlink():
        raise ValueError(f"source FAT32 image must be a regular file: {source}")
    if output.exists() or output.is_symlink():
        raise FileExistsError(f"refusing to overwrite existing output: {output}")
    if not output.parent.is_dir():
        raise FileNotFoundError(f"output directory does not exist: {output.parent}")
    if report.exists() or report.is_symlink():
        raise FileExistsError(f"refusing to overwrite existing report: {report}")

    source_size = source.stat().st_size
    if source_size <= 0 or source_size % SECTOR:
        raise ValueError("source FAT32 image size must be a positive multiple of 512 bytes")
    with source.open("rb") as stream:
        boot_sector = stream.read(SECTOR)
    if (
        len(boot_sector) != SECTOR
        or struct.unpack_from("<H", boot_sector, 11)[0] != SECTOR
        or boot_sector[82:90].rstrip(b" ") != b"FAT32"
        or boot_sector[510:512] != b"\x55\xaa"
    ):
        raise ValueError("source image does not contain a valid FAT32 boot sector")

    partition_sectors = source_size // SECTOR
    partition_first = PARTITION_START
    partition_last = partition_first + partition_sectors - 1
    total_sectors = partition_last + GPT_ENTRY_SECTORS + 2
    backup_entries_lba = total_sectors - GPT_ENTRY_SECTORS - 1
    first_usable = GPT_ENTRY_SECTORS + 2
    last_usable = backup_entries_lba - 1
    if partition_last > last_usable:
        raise ValueError("EFI partition does not fit within GPT usable LBAs")

    source_digest = sha256(source)
    disk_guid = uuid.uuid5(uuid.NAMESPACE_OID, f"PST3-disk:{source_digest}")
    partition_guid = uuid.uuid5(uuid.NAMESPACE_OID, f"PST3-esp:{source_digest}")
    entries = make_partition_entry(partition_guid, partition_first, partition_last)
    entries += bytes(GPT_ENTRY_BYTES - len(entries))
    entries_crc = zlib.crc32(entries) & 0xFFFFFFFF
    primary_header = make_gpt_header(
        current_lba=1, backup_lba=total_sectors - 1,
        first_usable=first_usable, last_usable=last_usable,
        disk_guid=disk_guid, entries_lba=2, entries_crc=entries_crc,
    )
    backup_header = make_gpt_header(
        current_lba=total_sectors - 1, backup_lba=1,
        first_usable=first_usable, last_usable=last_usable,
        disk_guid=disk_guid, entries_lba=backup_entries_lba, entries_crc=entries_crc,
    )

    with output.open("xb") as target:
        target.truncate(total_sectors * SECTOR)
        target.seek(0)
        target.write(make_protective_mbr(total_sectors))
        target.seek(SECTOR)
        target.write(primary_header)
        target.seek(2 * SECTOR)
        target.write(entries)
        target.seek(partition_first * SECTOR)
        with source.open("rb") as origin:
            for block in iter(lambda: origin.read(1024 * 1024), b""):
                target.write(block)
        target.seek(partition_first * SECTOR + 28)
        target.write(struct.pack("<I", partition_first))
        target.seek(backup_entries_lba * SECTOR)
        target.write(entries)
        target.seek((total_sectors - 1) * SECTOR)
        target.write(backup_header)
        target.flush()
        os.fsync(target.fileno())

    result = verify_image(output, source, partition_first, partition_last, entries_crc)
    report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def verify_image(
    image: Path, source: Path, partition_first: int, partition_last: int, entries_crc: int
) -> dict[str, object]:
    total_sectors = image.stat().st_size // SECTOR
    if image.stat().st_size % SECTOR or total_sectors < 40:
        raise ValueError("output image has an invalid sector length")
    with image.open("rb") as stream:
        mbr = stream.read(SECTOR)
        stream.seek(SECTOR)
        primary = stream.read(SECTOR)
        stream.seek(2 * SECTOR)
        primary_entries = stream.read(GPT_ENTRY_BYTES)
        stream.seek(partition_first * SECTOR)
        copied_boot = stream.read(SECTOR)
        stream.seek((total_sectors - 1) * SECTOR)
        backup = stream.read(SECTOR)
        backup_entries_lba = struct.unpack_from("<Q", backup, 72)[0]
        stream.seek(backup_entries_lba * SECTOR)
        backup_entries = stream.read(GPT_ENTRY_BYTES)
        stream.seek(partition_first * SECTOR)
        copied_digest = hashlib.sha256()
        source_stream = source.open("rb")
        remaining = source.stat().st_size
        try:
            while remaining:
                amount = min(1024 * 1024, remaining)
                block = stream.read(amount)
                source_block = source_stream.read(amount)
                if len(block) != amount or len(source_block) != amount:
                    raise ValueError("short read while checking the EFI partition")
                if remaining == source.stat().st_size:
                    source_block = bytearray(source_block)
                    source_block[28:32] = struct.pack("<I", partition_first)
                    source_block = bytes(source_block)
                if block != source_block:
                    raise ValueError("EFI System Partition differs from the source FAT32 image")
                copied_digest.update(block)
                remaining -= amount
        finally:
            source_stream.close()

    for label, header, expected_current in (
        ("primary", primary, 1), ("backup", backup, total_sectors - 1)
    ):
        if header[0:8] != b"EFI PART" or struct.unpack_from("<I", header, 12)[0] != 92:
            raise ValueError(f"{label} GPT header signature or size is invalid")
        stored_crc = struct.unpack_from("<I", header, 16)[0]
        check = bytearray(header[:92])
        struct.pack_into("<I", check, 16, 0)
        if zlib.crc32(check) & 0xFFFFFFFF != stored_crc:
            raise ValueError(f"{label} GPT header CRC is invalid")
        if struct.unpack_from("<Q", header, 24)[0] != expected_current:
            raise ValueError(f"{label} GPT header has the wrong current LBA")
    if mbr[510:512] != b"\x55\xaa" or mbr[450] != 0xEE:
        raise ValueError("protective MBR is invalid")
    if primary_entries != backup_entries or zlib.crc32(primary_entries) & 0xFFFFFFFF != entries_crc:
        raise ValueError("primary and backup GPT partition entries do not match")
    if primary_entries[:16] != EFI_SYSTEM_PARTITION_GUID.bytes_le:
        raise ValueError("first GPT partition is not an EFI System Partition")
    if struct.unpack_from("<QQ", primary_entries, 32) != (partition_first, partition_last):
        raise ValueError("EFI partition boundaries do not match the source FAT32 image")
    if struct.unpack_from("<Q", primary, 72)[0] != 2:
        raise ValueError("primary GPT partition table is not at LBA 2")
    if copied_boot[82:90].rstrip(b" ") != b"FAT32" or copied_boot[510:512] != b"\x55\xaa":
        raise ValueError("EFI System Partition FAT32 boot sector is invalid")
    if struct.unpack_from("<I", copied_boot, 28)[0] != partition_first:
        raise ValueError("FAT32 hidden-sector count does not match the GPT partition start")
    return {
        "status": "PASS",
        "partitionType": str(EFI_SYSTEM_PARTITION_GUID),
        "firstLba": partition_first,
        "lastLba": partition_last,
        "fat32Bytes": source.stat().st_size,
        "diskBytes": image.stat().st_size,
        "diskSha256": sha256(image),
        "sourceFat32Sha256": sha256(source),
        "partitionFat32Sha256": copied_digest.hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-fat32", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    result = create_image(args.source_fat32, args.output, args.report)
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
