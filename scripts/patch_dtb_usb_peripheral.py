#!/usr/bin/env python3
"""
Deterministic FDT (Device Tree Blob) patcher for MagicX XU10 (RK3326S).
Forces USB OTG controller (/usb@ff300000) into peripheral (USB Gadget) mode
while keeping /syscon@ff2c0000/usb2phy@100/otg-port disabled so that
phy-rockchip-inno-usb2 does not suspend the PHY due to missing utmi_bvalid wiring.
"""

import struct
import sys
from pathlib import Path


def patch_fdt(data: bytes, target_changes: dict[tuple[str, str], bytes]) -> tuple[bytes, list[str]]:
    magic, totalsize, off_dt_struct, off_dt_strings, off_mem_rsvmap, version, last_comp_version, boot_cpuid_phys, size_dt_strings, size_dt_struct = struct.unpack(
        ">10I", data[:40]
    )
    if magic != 0xD00DFEED:
        raise ValueError(f"Invalid FDT magic: 0x{magic:08x}")

    rsvmap_chunk = data[off_mem_rsvmap:off_dt_struct]
    strings_chunk = data[off_dt_strings : off_dt_strings + size_dt_strings]

    def get_str(offset: int) -> str:
        end = strings_chunk.find(b"\x00", offset)
        return strings_chunk[offset:end].decode("ascii", errors="replace")

    pos = off_dt_struct
    end_struct = off_dt_struct + size_dt_struct
    path: list[str] = []
    new_struct = bytearray()
    applied_logs: list[str] = []

    while pos < end_struct:
        token_bytes = data[pos : pos + 4]
        token = struct.unpack(">I", token_bytes)[0]
        pos += 4

        if token == 1:  # FDT_BEGIN_NODE
            end = data.find(b"\x00", pos)
            name = data[pos:end].decode("ascii", errors="replace")
            path.append(name)
            next_pos = (end + 4) & ~3
            new_struct.extend(token_bytes)
            new_struct.extend(data[pos:next_pos])
            pos = next_pos
        elif token == 2:  # FDT_END_NODE
            path.pop()
            new_struct.extend(token_bytes)
        elif token == 3:  # FDT_PROP
            plen, nameoff = struct.unpack(">II", data[pos : pos + 8])
            pos += 8
            pval = data[pos : pos + plen]
            pos = (pos + plen + 3) & ~3
            pname = get_str(nameoff)
            full_path = "/".join(path)

            key = (full_path, pname)
            if key in target_changes:
                new_val = target_changes[key]
                applied_logs.append(f"{full_path} :: {pname}: {pval!r} -> {new_val!r}")
                pval = new_val
                plen = len(new_val)

            new_struct.extend(token_bytes)
            new_struct.extend(struct.pack(">II", plen, nameoff))
            new_struct.extend(pval)
            pad_len = ((plen + 3) & ~3) - plen
            if pad_len:
                new_struct.extend(b"\x00" * pad_len)
        elif token == 4:  # FDT_NOP
            new_struct.extend(token_bytes)
        elif token == 9:  # FDT_END
            new_struct.extend(token_bytes)
            break
        else:
            raise ValueError(f"Unknown FDT token {token} at offset 0x{pos-4:x}")

    new_size_dt_struct = len(new_struct)
    new_off_dt_strings = off_dt_struct + new_size_dt_struct
    new_totalsize = new_off_dt_strings + size_dt_strings

    new_header = struct.pack(
        ">10I",
        magic,
        new_totalsize,
        off_dt_struct,
        new_off_dt_strings,
        off_mem_rsvmap,
        version,
        last_comp_version,
        boot_cpuid_phys,
        size_dt_strings,
        new_size_dt_struct,
    )

    out = bytes(new_header) + rsvmap_chunk + bytes(new_struct) + strings_chunk
    assert len(out) == new_totalsize, f"Size mismatch: {len(out)} != {new_totalsize}"
    return out, applied_logs


def main() -> None:
    src_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("backups/rk3326-magicx-xu10.dtb.orig")
    dst_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("backups/rk3326-magicx-xu10.dtb.peripheral")

    raw = src_path.read_bytes()
    changes = {
        ("/usb@ff300000", "dr_mode"): b"peripheral\x00",
    }
    patched, logs = patch_fdt(raw, changes)
    dst_path.write_bytes(patched)
    print(f"Patched DTB written to {dst_path} ({len(patched)} bytes):")
    for line in logs:
        print(f"  [OK] {line}")


if __name__ == "__main__":
    main()
