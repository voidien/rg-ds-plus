#!/usr/bin/env python3
"""Unpack an Anbernic RG DS / RG DS Plus Linux SD-card image for analysis.

Usage: extract_firmware.py IMAGE OUTDIR [--partition NAME ...]

Reads the GPT, extracts the loader and the Rockchip FIT partitions (uboot, boot,
recovery), and from them the kernel, embedded kernel config, resource container
and device trees. Big filesystem partitions are only extracted on request.
Needs only the Python standard library; 'dtc' is used to decompile device trees
when it is installed.

Treat the image as untrusted data: OUTDIR must be a new, empty directory.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib

SECTOR = 512
FDT_MAGIC = 0xD00DFEED


def read_gpt(f):
    f.seek(SECTOR)
    hdr = f.read(SECTOR)
    if hdr[:8] != b"EFI PART":
        sys.exit("no GPT found")
    entries_lba = struct.unpack("<Q", hdr[72:80])[0]
    count, size = struct.unpack("<II", hdr[80:88])
    f.seek(entries_lba * SECTOR)
    parts = []
    for i in range(count):
        e = f.read(size)
        if e[:16] == b"\0" * 16:
            continue
        first, last = struct.unpack("<QQ", e[32:48])
        name = e[56:128].decode("utf-16le").rstrip("\0")
        parts.append({"index": i + 1, "name": name, "first_lba": first,
                      "sectors": last - first + 1})
    return parts


def dump(f, first_lba, sectors, path, chunk=1 << 24):
    f.seek(first_lba * SECTOR)
    left = sectors * SECTOR
    with open(path, "wb") as out:
        while left:
            buf = f.read(min(chunk, left))
            if not buf:
                break
            out.write(buf)
            left -= len(buf)


# --- minimal flattened-device-tree reader (enough for FIT image nodes) ------

def parse_fdt(blob):
    """Return the tree as nested dicts: {'props': {name: bytes}, 'kids': {name: node}}."""
    magic, total, off_struct, off_str = struct.unpack(">IIII", blob[:16])
    if magic != FDT_MAGIC:
        raise ValueError("not an FDT")
    strings = blob[off_str:]
    pos = off_struct
    root = {"props": {}, "kids": {}}
    stack = [root]
    while True:
        tag = struct.unpack(">I", blob[pos:pos + 4])[0]
        pos += 4
        if tag == 1:  # BEGIN_NODE
            end = blob.index(b"\0", pos)
            name = blob[pos:end].decode()
            pos = (end + 4) & ~3
            node = {"props": {}, "kids": {}}
            stack[-1]["kids"][name] = node
            stack.append(node)
        elif tag == 2:  # END_NODE
            stack.pop()
        elif tag == 3:  # PROP
            length, nameoff = struct.unpack(">II", blob[pos:pos + 8])
            pos += 8
            pname = strings[nameoff:strings.index(b"\0", nameoff)].decode()
            stack[-1]["props"][pname] = blob[pos:pos + length]
            pos = (pos + length + 3) & ~3
        elif tag == 9:  # END
            return root["kids"][""] if "" in root["kids"] else root
        # tag 4 (NOP): nothing to do


def u32(b):
    return struct.unpack(">I", b)[0]


def extract_fit(path, outdir, tag):
    """Split a Rockchip FIT with external data into its component images."""
    blob = open(path, "rb").read()
    tree = parse_fdt(blob)
    images = tree["kids"].get("images", {}).get("kids", {})
    result = {"description": tree["props"].get("description", b"").rstrip(b"\0").decode(errors="replace"),
              "images": {}, "config_signed": False}
    conf = tree["kids"].get("configurations", {}).get("kids", {})
    for c in conf.values():
        if "signature" in c["kids"]:
            result["config_signed"] = True
    for name, img in images.items():
        p = img["props"]
        if "data-position" not in p or "data-size" not in p:
            continue
        pos, size = u32(p["data-position"]), u32(p["data-size"])
        data = blob[pos:pos + size]
        fn = os.path.join(outdir, "%s_%s.bin" % (tag, name))
        with open(fn, "wb") as o:
            o.write(data)
        result["images"][name] = {"offset": pos, "size": size,
                                  "compression": p.get("compression", b"").rstrip(b"\0").decode()}
    return result


def kernel_info(kernel_path, outdir):
    d = open(kernel_path, "rb").read()
    info = {}
    i = d.find(b"Linux version ")
    if i >= 0:
        info["version"] = d[i:d.index(b"\n", i)].decode(errors="replace")
    i = d.find(b"IKCFG_ST")
    if i >= 0:
        try:
            cfg = zlib.decompressobj(31).decompress(d[i + 8:i + 8 + (1 << 20)])
            with open(os.path.join(outdir, "kernel.config"), "wb") as o:
                o.write(cfg)
            info["config_bytes"] = len(cfg)
        except zlib.error:
            pass
    return info


def decompile(dtb, dts):
    if shutil.which("dtc"):
        subprocess.run(["dtc", "-I", "dtb", "-O", "dts", "-o", dts, dtb],
                       stderr=subprocess.DEVNULL, check=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("image")
    ap.add_argument("outdir")
    ap.add_argument("--partition", action="append", default=[],
                    help="also dump this partition by name (e.g. rootfs, vendor)")
    a = ap.parse_args()

    if os.path.exists(a.outdir) and os.listdir(a.outdir):
        sys.exit("OUTDIR must be empty")
    os.makedirs(a.outdir, exist_ok=True)
    report = {"image": os.path.basename(a.image)}

    with open(a.image, "rb") as f:
        parts = read_gpt(f)
        report["partitions"] = parts
        by_name = {p["name"]: p for p in parts}
        # idbloader (DRAM init + SPL) lives at sector 64, before the first partition
        first = min(p["first_lba"] for p in parts)
        dump(f, 64, first - 64, os.path.join(a.outdir, "idbloader.bin"))
        for name in ("uboot", "boot", "recovery") + tuple(a.partition):
            p = by_name.get(name)
            if p:
                dump(f, p["first_lba"], p["sectors"], os.path.join(a.outdir, name + ".img"))
            else:
                print("warning: no partition named", name, file=sys.stderr)

    for name in ("boot", "recovery", "uboot"):
        path = os.path.join(a.outdir, name + ".img")
        if not os.path.exists(path):
            continue
        try:
            fit = extract_fit(path, a.outdir, name)
        except (ValueError, IndexError, struct.error) as e:
            report[name] = {"error": str(e)}
            continue
        report[name] = fit
        for img in fit["images"]:
            fn = os.path.join(a.outdir, "%s_%s.bin" % (name, img))
            if img == "fdt":
                decompile(fn, fn[:-4] + ".dts")
            elif img == "kernel":
                report[name]["kernel"] = kernel_info(fn, a.outdir)

    # Does U-Boot's own DT carry a verified-boot public key?
    ub = os.path.join(a.outdir, "uboot_fdt.bin")
    if os.path.exists(ub):
        t = parse_fdt(open(ub, "rb").read())
        report["uboot_has_signature_node"] = "signature" in t["kids"]

    with open(os.path.join(a.outdir, "report.json"), "w") as o:
        json.dump(report, o, indent=2)
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
