"""Inspect a paused 32-bit DAMON_STAT refresh in QEMU's GDB stub.

The structure offsets and breakpoint addresses correspond to the tested
vmlinux SHA-256 ef00c9426cfbb366c6113f54e874fb7c0835a4a7059efd4a93fa8f0e8efff02b.
"""

import gdb


_hits = 0


def _u32(address):
    return int.from_bytes(gdb.selected_inferior().read_memory(address, 4), "little")


def bandwidth32_probe():
    global _hits
    _hits += 1
    ebp = int(gdb.parse_and_eval("$ebp")) & 0xFFFFFFFF
    access_bytes = int(gdb.parse_and_eval("$ecx")) & 0xFFFFFFFF
    aggr_us = int(gdb.parse_and_eval("$ebx")) & 0xFFFFFFFF
    quotient = int(gdb.parse_and_eval("$eax")) & 0xFFFFFFFF
    stored = _u32(0xC13D2B34)
    ctx = _u32(ebp - 0x18)
    target_head = ctx + 0xB4
    target_link = _u32(target_head)
    sum64 = total_bytes = nr_regions = active_regions = nr_targets = 0

    while target_link != target_head:
        nr_targets += 1
        if nr_targets > 16:
            raise RuntimeError("target list traversal limit")
        target = target_link - 0x14
        region_head = target + 0xC
        region_link = _u32(region_head)
        while region_link != region_head:
            nr_regions += 1
            if nr_regions > 10000:
                raise RuntimeError("region list traversal limit")
            region = region_link - 0x18
            size = _u32(region + 4) - _u32(region)
            accesses = _u32(region + 0xC)
            total_bytes += size
            sum64 += size * accesses
            active_regions += int(accesses > 0)
            region_link = _u32(region + 0x18)
        target_link = _u32(target + 0x14)

    wrapped_product = (access_bytes * 1000000) & 0xFFFFFFFF
    expected = sum64 * 1000000 // aggr_us
    print(
        f"BWPROBE hit={_hits} access_bytes={access_bytes} sum64={sum64} "
        f"sum_wrap={int(sum64 >= 2**32)} total_region_bytes={total_bytes} "
        f"targets={nr_targets} regions={nr_regions} active_regions={active_regions} "
        f"aggr_us={aggr_us} wrapped_product={wrapped_product} "
        f"quotient_bps={quotient} param_bps={stored} correct_bps={expected} "
        f"correct_fits_ulong={int(expected <= 0xFFFFFFFF)} "
        f"sum_matches={int((sum64 & 0xFFFFFFFF) == access_bytes)} "
        f"division_matches={int(wrapped_product // aggr_us == quotient)}",
        flush=True,
    )
