# Copyright (c) Andrew Rankin 2026-06-12.

"""Print -DFLASH_<PART>_OFFSET/_LENGTH flags for a board JSON flash map.

Mirrors builder/utils/flash.py so the host tests use the board's real layout.
"""

import json
import sys

with open(sys.argv[1]) as f:
    flash = json.load(f)["flash"]
flags = []
for name, layout in flash.items():
    offset, _, length = layout.partition("+")
    flags.append(f"-DFLASH_{name.upper()}_OFFSET=0x{int(offset, 16):06X}")
    flags.append(f"-DFLASH_{name.upper()}_LENGTH=0x{int(length, 16):06X}")
print(" ".join(flags))
