#!/usr/bin/env python3
import struct
import sys

FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9

def align4(b: bytes) -> bytes:
    rem = len(b) % 4
    return b + (b"\x00" * ((4 - rem) % 4))

def get_or_add_string(strings_blob: bytearray, name: str) -> int:
    target = name.encode("utf-8") + b"\x00"
    idx = 0
    while idx < len(strings_blob):
        end = strings_blob.index(b"\x00", idx)
        if strings_blob[idx:end+1] == target:
            return idx
        idx = end + 1
    off = len(strings_blob)
    strings_blob.extend(target)
    return off

def make_prop(strings_blob: bytearray, name: str, val: bytes) -> bytes:
    nameoff = get_or_add_string(strings_blob, name)
    return struct.pack(">III", FDT_PROP, len(val), nameoff) + align4(val)

def patch_dtb(in_path: str, out_path: str):
    data = open(in_path, "rb").read()
    (
        magic,
        totalsize,
        off_dt_struct,
        off_dt_strings,
        off_mem_rsvmap,
        version,
        last_comp_version,
        boot_cpuid_phys,
        size_dt_strings,
        size_dt_struct,
    ) = struct.unpack(">10I", data[:40])

    assert magic == 0xD00DFEED, "Invalid FDT magic"

    rsvmap_blob = data[off_mem_rsvmap:off_dt_struct]
    struct_blob = data[off_dt_struct:off_dt_struct + size_dt_struct]
    strings_blob = bytearray(data[off_dt_strings:off_dt_strings + size_dt_strings])

    new_struct = bytearray()
    pos = 0
    stack = []
    otg_phandle = 0x13B

    has_otg_switch = b"OTG_SWITCH\x00" in struct_blob

    while pos < len(struct_blob):
        token = struct.unpack(">I", struct_blob[pos:pos+4])[0]
        if token == FDT_BEGIN_NODE:
            end = struct_blob.index(b"\x00", pos + 4)
            name = struct_blob[pos+4:end].decode("utf-8")
            next_pos = (end + 4) & ~3
            new_struct.extend(struct_blob[pos:next_pos])
            pos = next_pos
            stack.append(name)
            cur_path = "/" + "/".join(stack[1:])
            if cur_path == "/usb@ff300000" and not has_otg_switch:
                new_struct.extend(make_prop(strings_blob, "vbus-supply", struct.pack(">I", otg_phandle)))
        elif token == FDT_END_NODE:
            cur_path = "/" + "/".join(stack[1:])
            if cur_path == "/i2c@ff180000/pmic@20/regulators" and not has_otg_switch:
                # Insert OTG_SWITCH child node before closing regulators node
                node_hdr = struct.pack(">I", FDT_BEGIN_NODE) + align4(b"OTG_SWITCH\x00")
                p1 = make_prop(strings_blob, "regulator-name", b"otg_switch\x00")
                p2 = make_prop(strings_blob, "regulator-boot-on", b"")
                p3 = make_prop(strings_blob, "phandle", struct.pack(">I", otg_phandle))
                node_end = struct.pack(">I", FDT_END_NODE)
                new_struct.extend(node_hdr + p1 + p2 + p3 + node_end)
            new_struct.extend(struct_blob[pos:pos+4])
            pos += 4
            stack.pop()
        elif token == FDT_PROP:
            plen, nameoff = struct.unpack(">II", struct_blob[pos+4:pos+12])
            next_pos = (pos + 12 + plen + 3) & ~3
            new_struct.extend(struct_blob[pos:next_pos])
            pos = next_pos
        elif token == FDT_NOP:
            new_struct.extend(struct_blob[pos:pos+4])
            pos += 4
        elif token == FDT_END:
            new_struct.extend(struct_blob[pos:pos+4])
            break
        else:
            raise RuntimeError(f"Unknown FDT token {token} at {pos}")

    new_off_rsvmap = 40
    new_off_struct = new_off_rsvmap + len(rsvmap_blob)
    new_size_struct = len(new_struct)
    new_off_strings = new_off_struct + new_size_struct
    new_size_strings = len(strings_blob)
    new_totalsize = new_off_strings + new_size_strings

    header = struct.pack(
        ">10I",
        magic,
        new_totalsize,
        new_off_struct,
        new_off_strings,
        new_off_rsvmap,
        version,
        last_comp_version,
        boot_cpuid_phys,
        new_size_strings,
        new_size_struct,
    )

    with open(out_path, "wb") as f:
        f.write(header + rsvmap_blob + new_struct + strings_blob)
    print(f"Patched {in_path} -> {out_path} ({len(data)} -> {new_totalsize} bytes)")

if __name__ == "__main__":
    patch_dtb(sys.argv[1], sys.argv[2])
