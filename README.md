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
hackbgrt (hd0,gpt1) image=/relative/path/to/bmp|keep|remove[,x=123|center|keep,y=456|center|keep][,weight=1] [image=...]*
```

Where:

- `(hd0,gpt1)` is your `ESP` (EFI System Partition) as seen by GRUB2.
- `image` variable could take a 24-bit BMP splash path file, or the value `keep`, or the value `remove`.
- `x` and `y` variables could be used to position the image. You can use an *absolute* position, or `center` value or `keep` value.
- `weight` variable is use to add a weight (probability) to your image. Only useful if you use multiple `image` variables.

The Splash file should be **relative to the ESP partition** and should **start with a slash**.
