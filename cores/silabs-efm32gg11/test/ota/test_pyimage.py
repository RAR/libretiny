# Copyright (c) Andrew Rankin 2026-06-12.

"""Check that builder/utils/efm32gg11_image.py and lt_ota_image.c agree.

Run by the Makefile: python3 test_pyimage.py ./test_pyimage ../../../../builder/utils/efm32gg11_image.py
"""

import importlib.util
import struct
import subprocess
import sys
import tempfile
from os.path import join


def load_module(path: str):
    sys.dont_write_bytecode = True  # no __pycache__ next to the builder
    spec = importlib.util.spec_from_file_location("efm32gg11_image", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_error(img, func, *args) -> None:
    try:
        func(*args)
    except img.ImageError:
        return
    raise AssertionError(f"{func.__name__}{args[1:]} accepted a bad image")


def make_elf(segments: list) -> bytes:
    """Minimal little-endian ELF32 (ARM) with one PT_LOAD per (paddr, data)."""
    phoff = 52
    data_off = 0x200
    phdrs = b""
    body = b""
    for paddr, data in segments:
        offset = data_off + len(body)
        phdrs += struct.pack("<8I", 1, offset, paddr, paddr, len(data), len(data), 5, 4)
        body += data
    header = b"\x7fELF" + bytes([1, 1, 1]) + bytes(9)
    header += struct.pack(
        "<HHIIIIIHHHHHH",
        2,  # ET_EXEC
        40,  # EM_ARM
        1,
        0,
        phoff,
        0,
        0,
        52,
        32,
        len(segments),
        40,
        0,
        0,
    )
    elf = header + phdrs
    return elf + bytes(data_off - len(elf)) + body


def test_elf(img, raw: bytes, link_addr: int) -> None:
    # the image split like a real link: code/rodata, then .data's load image
    split = len(raw) - 0x70
    elf = make_elf(
        [
            (0x1000, b"\x00" * 16),
            (link_addr, raw[:split]),
            (link_addr + split, raw[split:]),
        ]
    )
    out = img.fill_elf(elf, raw, link_addr)
    assert len(out) == len(elf)
    # extract the loaded bytes again and verify them
    loaded = bytearray(len(raw))
    phoff = 52
    for i in range(3):
        _, offset, _, paddr, filesz = struct.unpack_from("<5I", out, phoff + i * 32)
        if paddr >= link_addr:
            loaded[paddr - link_addr : paddr - link_addr + filesz] = out[
                offset : offset + filesz
            ]
    img.check(bytes(loaded), link_addr)
    assert bytes(loaded) == img.fill(raw, link_addr)
    # an ELF whose descriptor differs from the binary it is said to come from
    other = bytearray(raw)
    other[img.DESC_OFFSET + 12] ^= 1
    expect_error(img, img.fill_elf, elf, bytes(other), link_addr)
    # no segment holds the descriptor
    expect_error(img, img.fill_elf, make_elf([(0x1000, raw)]), raw, link_addr)
    expect_error(img, img.elf_file_offset, b"\x7fELF\x02\x01" + bytes(60), 0, 4)


def main() -> int:
    exe, module_path = sys.argv[1], sys.argv[2]
    img = load_module(module_path)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([exe, "gen", tmp], check=True)
        with open(join(tmp, "manifest.txt")) as f:
            banks = [line.split() for line in f]
        addrs = {bank: int(addr, 0) for bank, addr in banks}
        for bank, addr in banks:
            raw = open(join(tmp, f"raw_{bank}.bin"), "rb").read()
            c_filled = open(join(tmp, f"c_{bank}.bin"), "rb").read()
            # the command line, as a user would run it
            py_path = join(tmp, f"py_{bank}.bin")
            with open(py_path, "wb") as f:
                f.write(raw)
            cli = [sys.executable, module_path]
            subprocess.run(cli + ["fill", py_path, addr], check=True)
            subprocess.run(cli + ["check", py_path, addr], check=True)
            subprocess.run(
                cli + ["check", join(tmp, f"c_{bank}.bin"), addr], check=True
            )
            r = subprocess.run(
                cli + ["check", join(tmp, f"raw_{bank}.bin"), addr],
                capture_output=True,
            )
            assert r.returncode == 1, "an unfilled image passed the check"
            # Python and C fill in the same bytes
            py_filled = open(py_path, "rb").read()
            assert (
                py_filled == c_filled
            ), f"bank {bank}: Python and C descriptors differ"

            # the checks the builder relies on
            link = addrs[bank]
            img.check(py_filled, link)
            other_link = [a for b, a in addrs.items() if b != bank][0]
            expect_error(img, img.check, py_filled, other_link)  # other bank
            expect_error(
                img, img.check, py_filled[:-1], link
            )  # binary shorter than its descriptor says
            expect_error(img, img.check, py_filled + b"\xff", link)  # longer
            flipped = bytearray(py_filled)
            flipped[len(flipped) // 2] ^= 0x10
            expect_error(img, img.check, bytes(flipped), link)
            expect_error(img, img.fill, b"\x00" * 0x1000, link)  # no descriptor
            words = struct.unpack_from("<2I", py_filled, img.CRC_OFFSET)
            assert all(w <= 0xFFFF for w in words)
            test_elf(img, raw, link)
        subprocess.run([exe, "check", tmp], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
