set pagination off
set confirm off
set debuginfod enabled off
file ./build-stat32/vmlinux
source ./bandwidth32-probe.py
target remote | timeout 65s qemu-system-i386 -accel tcg -m 256M -smp 1 -kernel ./build-stat32/arch/x86/boot/bzImage -initrd ./bandwidth32-initramfs.cpio -append 'console=ttyS0 rdinit=/init loglevel=5 panic=1' -display none -serial file:./bandwidth32-guest.log -monitor none -no-reboot -nic none -S -gdb stdio
break *0xc114dd35
commands
  silent
  python bandwidth32_probe()
  continue
end
continue
