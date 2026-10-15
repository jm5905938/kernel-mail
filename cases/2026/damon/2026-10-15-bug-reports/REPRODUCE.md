# Reproduce

Source: `mm-new` commit `ad9f2d463666c64e94b04be5263c9bf8f9f96fb7`.
For each section, copy that case directory's files into a fresh scratch
directory. Use the included full `.config` and set `KERNEL` to the source
checkout. Required tools: GCC, make, cpio, QEMU; i386 builds need multilib.
Section 4 also needs GDB with Python support. Section 5 uses
`supplemental-arithmetic/` and `qemu-i386`.

1. sysfs snapshot loss, x86_64 guest
------------------------------------

Run the following from a new scratch directory containing the attachments:

```sh
  KERNEL=/path/to/linux
  BUILD="$PWD/build-sysfs"
  mkdir -p "$BUILD" ramfs-sysfs
  cp sysfs-guest.config "$BUILD/.config"
  make -C "$KERNEL" ARCH=x86 O="$BUILD" olddefconfig
  make -C "$KERNEL" -j8 ARCH=x86 O="$BUILD" bzImage
  gcc -static -O2 -Wall -Wextra -o ramfs-sysfs/init \
      sysfs-tried-snapshot-after-off.c
  (cd ramfs-sysfs && find . -print0 | cpio --null -o --format=newc) \
      > sysfs-initramfs.cpio
  qemu-system-x86_64 -machine accel=tcg -m 256M -smp 2 \
      -display none -serial stdio -monitor none -no-reboot \
      -kernel "$BUILD/arch/x86/boot/bzImage" \
      -initrd sysfs-initramfs.cpio \
      -append 'console=ttyS0 rdinit=/init panic=-1'
```

The tested guest printed (the number of region directories may vary):

```text
  BEFORE_OFF bytes=4194304 dirs=10
  STATE_OFF bytes=4194304 dirs=10
  FAILED_UPDATE ret=-1 errno=125 bytes_after=0 dirs_after=0
  RESULT exit=0
```

The guest reboots itself.  -no-reboot makes QEMU exit after the result.
The public serial log replaces only its first-line local build user and host.

2. Intervals goal overflow, full i386 kernel and online kdamond
---------------------------------------------------------------

From a new scratch directory containing the attachments:

```sh
  KERNEL=/path/to/linux
  BUILD="$PWD/build-intervals32"
  mkdir -p "$BUILD" ramfs-intervals32
  cp intervals32-runtime.config "$BUILD/.config"
  make -C "$KERNEL" ARCH=x86 O="$BUILD" olddefconfig
  make -C "$KERNEL" -j4 ARCH=x86 O="$BUILD" bzImage
  gcc -m32 -static -O2 -Wall -Wextra -D_TIME_BITS=64 \
      -D_FILE_OFFSET_BITS=64 -o ramfs-intervals32/init \
      intervals32-runtime-400-init.c
  (cd ramfs-intervals32 && find . -print0 | cpio --null -o --format=newc) \
      > intervals32-initramfs.cpio
  timeout 50s qemu-system-i386 -accel tcg -m 256M -smp 1 \
      -kernel "$BUILD/arch/x86/boot/bzImage" \
      -initrd intervals32-initramfs.cpio \
      -append 'console=ttyS0 rdinit=/init loglevel=4 panic=1' \
      -nographic -monitor none -no-reboot -nic none
```

The tested first-aggregation trace has four contiguous 262144-byte
regions with nr_accesses=1,1,1,3, followed on the same kdamond by
damon_monitor_intervals_tune: sample_us=2500.  With the configured
initial sample_us=5000, aggr_us=100000 and access_bp=400, full-width
calculation from those trace inputs gives 2812 us.  The saved tested
output is intervals32-runtime-400-output.log.  Later aggregations are
affected by the earlier tuning and are not needed for this comparison.
The guest prints INTERVALS32_GUEST_DONE and halts; a later timeout status
124 is expected for this minimal guest.

3. DAMON_STAT percentiles, i386 guest
------------------------------------

Use a separate scratch directory containing the attachments:

```sh
  KERNEL=/path/to/linux
  BUILD="$PWD/build-stat32"
  mkdir -p "$BUILD" ramfs-stat32/dev ramfs-stat32/proc ramfs-stat32/sys
  cp stat32-compat.config "$BUILD/.config"
  make -C "$KERNEL" ARCH=x86 O="$BUILD" olddefconfig
  make -C "$KERNEL" -j8 ARCH=x86 O="$BUILD" bzImage
  gcc -m32 -static -O2 -Wall -Wextra -D_TIME_BITS=64 \
      -D_FILE_OFFSET_BITS=64 -o ramfs-stat32/init stat32-init.c
  (cd ramfs-stat32 && find . -print0 | cpio --null -o --format=newc) \
      > stat32-initramfs.cpio
  timeout 50s qemu-system-i386 -accel tcg -m 256M -smp 1 \
      -kernel "$BUILD/arch/x86/boot/bzImage" \
      -initrd stat32-initramfs.cpio \
      -append 'console=ttyS0 rdinit=/init loglevel=5 panic=1' \
      -nographic -monitor none -no-reboot -nic none
```

Look for STAT32_GUEST_READY long_bits=32, ENABLED Y, changing
AGGR_INTERVAL values, and a nonmonotonic PERCENTILES result.  The tested
run printed p0=19200 p50=0 p100=0; exact idle times may vary.  The
minimal guest has no power-off device; it halts and timeout later ends
QEMU with status 124 after STAT32_GUEST_DONE.  The status is not a test
failure.  The attached stat32-guest-output.log is the tested output.
No per-region trace or patched-kernel comparison was collected for this case.

4. DAMON_STAT bandwidth overflow, full i386 kernel with GDB probe
-----------------------------------------------------------------

From a new scratch directory containing the attachments:

```sh
  KERNEL=/path/to/linux
  BUILD="$PWD/build-stat32"
  mkdir -p "$BUILD" ramfs-bandwidth32
  cp stat32-compat.config "$BUILD/.config"
  make -C "$KERNEL" ARCH=x86 O="$BUILD" olddefconfig
  make -C "$KERNEL" -j4 ARCH=x86 O="$BUILD" bzImage vmlinux
  gcc -m32 -static -O2 -Wall -Wextra -D_TIME_BITS=64 \
      -D_FILE_OFFSET_BITS=64 -o ramfs-bandwidth32/init \
      stat-bandwidth32-init.c
  (cd ramfs-bandwidth32 && find . -print0 | cpio --null -o --format=newc) \
      > bandwidth32-initramfs.cpio
```

The included replay script uses relative paths from this scratch
directory. The original local script is omitted because it contains
workstation-specific absolute paths. The GDB breakpoint 0xc114dd35,
the bandwidth parameter address 0xc13d2b34 and
the structure offsets in bandwidth32-probe.py are valid for the tested
vmlinux only, SHA-256
ef00c9426cfbb366c6113f54e874fb7c0835a4a7059efd4a93fa8f0e8efff02b.
The tested bzImage SHA-256 was
b6d285e8e36c03c0855613637d5376bab1bbc950ea273d74c77c6540583479a5.
The saved binary files are not in this evidence repository. A fresh build
can differ, so check both hashes before running the fixed-address script:

```sh
  VMLINUX_SHA=$(sha256sum "$BUILD/vmlinux" | cut -d' ' -f1)
  BZIMAGE_SHA=$(sha256sum "$BUILD/arch/x86/boot/bzImage" | cut -d' ' -f1)
  if [ "$VMLINUX_SHA" = ef00c9426cfbb366c6113f54e874fb7c0835a4a7059efd4a93fa8f0e8efff02b ] && \
     [ "$BZIMAGE_SHA" = b6d285e8e36c03c0855613637d5376bab1bbc950ea273d74c77c6540583479a5 ]; then
      timeout 75s gdb -q -batch -x bandwidth32-gdb-replay.gdb \
          > bandwidth32-gdb.log 2>&1
  else
      printf '%s\n' 'Rebuilt binaries differ; recheck probe addresses before replay.'
  fi
```

If the hashes differ, locate the store following the multiply and divide
in damon_stat_damon_call_fn, then recheck the breakpoint, parameter
symbol, and list/field offsets against the rebuilt vmlinux's symbols
and disassembly before updating and using the probe. The included
bandwidth32-disasm.log documents the tested binary only.

The corrected-command-line tested run is in bandwidth32-gdb-v2-output.log
and bandwidth32-guest-v2-output.log.  GDB prints BWPROBE rows with the
same callback's access_bytes, independently traversed sum64,
aggr_us=100000, stored parameter, and full-width correct_bps.  Three
nonzero rows show 31079 vs 333578240, 25244 vs 652001280, and 24303
vs 411525120 B/s; the guest finally reads 24303 from sysfs.  All three
full-width results fit unsigned long, and the region sum matches the
kernel's access_bytes with sum_wrap=0.  The guest prints
BANDWIDTH32_GUEST_DONE and halts; the inner QEMU timeout ends it later.

5. Supplemental 32-bit arithmetic checks
----------------------------------------

```sh
  gcc -m32 -O0 -Wall -Wextra -o arith32 damon-arith32.c
  qemu-i386 ./arith32
  gcc -m64 -O0 -Wall -Wextra -o arith64 damon-arith32.c
  ./arith64
```

The 32-bit run prints target=409364, score=25614, new_sample=2500 and
bandwidth=0.  The 64-bit run prints target=838860, score=12500,
new_sample=4375 and bandwidth=671088640.  These checks use a separate
synthetic input, so their score and interval numbers are not those of
the first-aggregation i386 kernel trace in section 2.  They supplement
the kernel reproductions in sections 2 and 4.
