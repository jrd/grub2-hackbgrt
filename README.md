Hackbgrt for GRUB 2
===================

Inspired by the work of [HackBGRT EFI application](https://github.com/Metabolix/HackBGRT.git).

This module is for `GRUB2` (x86 **32** or **64 bits**) and allow to modify the **EFI** BGRT at grub level.

The `BGRT` stands for **Boot Graphic Resource Table** and holds the boot logo you see when booting in [UEFI](https://wiki.osdev.org/UEFI).

This logo cannot be directly modified as it is builtin in the EFI firmware, unless you flash a new one.

However the ACPI table describing the logo could still be changed (not permanently) in an EFI application.
`GRUB2`, and its modules, are EFI applications and so can change the content of the `BGRT` table for a one-time-boot.

This is what this GRUB2 module do.

Usage
-----

```sh
insmod hackbgrt
hackbgrt (hd0,gpt1)
```

Where `(hd0,gpt1)` is your `ESP` (EFI System Partition) as seen by GRUB2.

In the `ESP`, a `/EFI/HackBGRT/config.txt` file should resides with the following content:

```
# image=keep
# The file must be a 24-bit BMP file with a 54-byte header.
# image=path=/EFI/HackBGRT/splash.bmp
image=path=/EFI/HackBGRT/splash.bmp
# Preferred resolution. Use 0x0 for maximum and -1x-1 for original.
resolution=0x0
```
