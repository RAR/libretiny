# Copyright (c) Andrew Rankin 2026-06-12.

"""Application image descriptor for silabs-efm32gg11.

Mirrors cores/silabs-efm32gg11/base/ota/lt_ota_image.h; the host tests in
cores/silabs-efm32gg11/test/ota check that both sides agree. Plain Python with
no SCons or PlatformIO imports, so that the tests can run it directly:

    python3 efm32gg11_image.py fill  IMAGE.bin LINK_ADDR  # fill in the CRC
    python3 efm32gg11_image.py check IMAGE.bin LINK_ADDR  # verify
"""

import struct
import sys
import zlib

DESC_OFFSET = 0x400
DESC_FORMAT = "<6I"  # magic, version, link_addr, length, crc_lo, crc_hi
DESC_SIZE = struct.calcsize(DESC_FORMAT)
CRC_OFFSET = DESC_OFFSET + 16  # crc_lo, crc_hi: 8 bytes left out of the CRC
MAGIC = 0x4D49544C
VERSION = 1

PT_LOAD = 1


class ImageError(ValueError):
    pass


def image_crc(image: bytes, length: int) -> int:
    """CRC-32 of image[0:length] without the 8 bytes of crc_lo/crc_hi."""
    crc = zlib.crc32(image[:CRC_OFFSET])
    return zlib.crc32(image[CRC_OFFSET + 8 : length], crc)


def read_desc(image: bytes, link_addr: int) -> tuple:
    """Check the link-time fields of the descriptor against the binary itself.

    Returns (magic, version, link_addr, length, crc_lo, crc_hi).
    """
    if len(image) < DESC_OFFSET + DESC_SIZE:
        raise ImageError(f"image too short for a descriptor ({len(image)} bytes)")
    desc = struct.unpack_from(DESC_FORMAT, image, DESC_OFFSET)
    magic, version, desc_link, length = desc[:4]
    if magic != MAGIC:
        raise ImageError(
            f"no image descriptor at offset 0x{DESC_OFFSET:X} "
            f"(magic 0x{magic:08X}, expected 0x{MAGIC:08X})"
        )
    if version < VERSION:
        raise ImageError(f"descriptor version {version} < {VERSION}")
    if desc_link != link_addr:
        raise ImageError(f"image linked at 0x{desc_link:X}, expected 0x{link_addr:X}")
    if length != len(image):
        raise ImageError(
            f"descriptor length {length} differs from the binary ({len(image)} bytes)"
        )
    return desc


def crc_words(image: bytes, link_addr: int) -> bytes:
    """The 8 bytes of crc_lo/crc_hi that belong in this image."""
    length = read_desc(image, link_addr)[3]
    crc = image_crc(image, length)
    return struct.pack("<2I", crc & 0xFFFF, crc >> 16)


def fill(image: bytes, link_addr: int) -> bytes:
    """Return the image with the CRC filled in."""
    words = crc_words(image, link_addr)
    return image[:CRC_OFFSET] + words + image[CRC_OFFSET + 8 :]


def check(image: bytes, link_addr: int) -> None:
    """Raise ImageError unless the image carries a correct descriptor."""
    if image[CRC_OFFSET : CRC_OFFSET + 8] != crc_words(image, link_addr):
        raise ImageError("image CRC does not match its descriptor")


def elf_file_offset(elf: bytes, addr: int, size: int) -> int:
    """File offset of the bytes loaded at physical (load) address addr.

    Little-endian ELF32 only. Uses the PT_LOAD program headers, which is what
    objcopy -O binary and a debugger load are based on.
    """
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise ImageError("not a little-endian ELF32 file")
    (phoff,) = struct.unpack_from("<I", elf, 0x1C)
    phentsize, phnum = struct.unpack_from("<HH", elf, 0x2A)
    for i in range(phnum):
        p_type, p_offset, _, p_paddr, p_filesz = struct.unpack_from(
            "<5I", elf, phoff + i * phentsize
        )
        if p_type == PT_LOAD and p_paddr <= addr and addr + size <= p_paddr + p_filesz:
            return p_offset + addr - p_paddr
    raise ImageError(f"no loadable segment holds 0x{addr:X}")


def fill_elf(elf: bytes, image: bytes, link_addr: int) -> bytes:
    """Return the ELF with the CRC of its binary image filled in.

    image is the ELF's objcopy -O binary output, which starts at link_addr.
    """
    words = crc_words(image, link_addr)
    start = link_addr + DESC_OFFSET
    off = elf_file_offset(elf, start, DESC_SIZE)
    if elf[off : off + 16] != image[DESC_OFFSET:CRC_OFFSET]:
        raise ImageError("descriptor in the ELF does not match its binary image")
    off += CRC_OFFSET - DESC_OFFSET
    return elf[:off] + words + elf[off + 8 :]


def main(argv: list) -> int:
    if len(argv) != 4 or argv[1] not in ("fill", "check"):
        sys.stderr.write(__doc__)
        return 2
    path, link_addr = argv[2], int(argv[3], 0)
    with open(path, "rb") as f:
        image = f.read()
    try:
        if argv[1] == "fill":
            image = fill(image, link_addr)
            with open(path, "wb") as f:
                f.write(image)
        else:
            check(image, link_addr)
    except ImageError as e:
        sys.stderr.write(f"{path}: {e}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
