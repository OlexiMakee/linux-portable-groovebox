#!/usr/bin/env python3
"""
Patch ROCKNIX-RK3326.aarch64-20261001-a.img in-place:
1. In rk3326-magicx-xu10.dtb:
   - /usb@ff300000 :: dr_mode: 'host' -> 'otg' (-4 aligned bytes)
   - /usb@ff300000 :: role-switch-default-mode: 'host' -> 'peripheral' (+4 aligned bytes)
   Total DTB size remains identically 68796 bytes.
2. In /extlinux/extlinux.conf:
   - Add 'ssh' to APPEND kernel command line so sshd.service starts automatically from first boot.
"""

import struct
from pathlib import Path
from patch_dtb_usb_peripheral import patch_fdt

IMG_PATH = Path("backups/ROCKNIX-RK3326.aarch64-20261001-a.img")
PART1_OFFSET = 32768 * 512


def main() -> None:
    with open(IMG_PATH, "r+b") as f:
        f.seek(PART1_OFFSET)
        bpb = f.read(512)
        bytes_per_sec = struct.unpack("<H", bpb[11:13])[0]
        sec_per_clus = bpb[13]
        rsvd_sec_cnt = struct.unpack("<H", bpb[14:16])[0]
        num_fats = bpb[16]
        fat_sz = struct.unpack("<I", bpb[36:40])[0]
        data_offset = PART1_OFFSET + (rsvd_sec_cnt + num_fats * fat_sz) * bytes_per_sec

        def clus_offset(c: int) -> int:
            return data_offset + (c - 2) * sec_per_clus * bytes_per_sec

        # 1. Patch rk3326-magicx-xu10.dtb (Cluster 123, size 68796 — contiguous clusters 123..139)
        dtb_clus = 123
        dtb_size = 68796
        orig_dtb = Path("backups/rk3326-magicx-xu10-20261001.dtb").read_bytes()

        changes = {
            ("/usb@ff300000", "dr_mode"): b"otg\x00",
            ("/usb@ff300000", "role-switch-default-mode"): b"peripheral\x00",
        }
        patched_dtb, logs = patch_fdt(orig_dtb, changes)
        assert len(patched_dtb) == dtb_size, f"DTB size changed: {len(patched_dtb)} != {dtb_size}"

        Path("backups/rk3326-magicx-xu10-20261001.dtb.patched").write_bytes(patched_dtb)
        f.seek(clus_offset(dtb_clus))
        f.write(patched_dtb)
        print(f"Patched rk3326-magicx-xu10.dtb in-place inside {IMG_PATH} ({len(patched_dtb)} bytes):")
        for l in logs:
            print(f"  [OK] {l}")

        # 2. Patch /extlinux/extlinux.conf (Cluster 273, size 364)
        ext_clus = 273
        ext_size = 364
        f.seek(clus_offset(ext_clus))
        ext_raw = f.read(ext_size).decode("ascii")
        if " ssh " not in ext_raw:
            new_ext = ext_raw.replace(
                "## FDT: don't forget the",
                "## FDT: note --- the",
            ).replace(
                "APPEND boot=${partition_boot} disk=${partition_storage} quiet",
                "APPEND boot=${partition_boot} disk=${partition_storage} ssh quiet",
            )
            assert len(new_ext.encode("ascii")) == ext_size, f"extlinux size mismatch: {len(new_ext)} != {ext_size}"
            f.seek(clus_offset(ext_clus))
            f.write(new_ext.encode("ascii"))
            print("Patched /extlinux/extlinux.conf in-place with 'ssh' bootarg:")
            print(new_ext)
        else:
            print("/extlinux/extlinux.conf already contains 'ssh' bootarg.")


if __name__ == "__main__":
    main()
