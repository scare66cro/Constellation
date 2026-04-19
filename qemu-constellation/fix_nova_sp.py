#!/usr/bin/env python3
"""Fix constellation_nova.c: set CPU SP after realize so startup code doesn't abort."""
import pathlib

p = pathlib.Path.home() / 'qemu-tm4c/hw/arm/am243x/constellation_nova.c'
t = p.read_text()

# After CPU realize, set SP to top of 2MB RAM
old = '    qdev_realize(DEVICE(s->cpu0), NULL, &error_fatal);'
new = '''    qdev_realize(DEVICE(s->cpu0), NULL, &error_fatal);

    /*
     * Pre-set SP (R13) to top of RAM.  On real silicon the ROM bootloader
     * does this; in QEMU the registers start at zero, which would cause
     * a Data Abort on the first push in Reset_Handler.
     */
    s->cpu0->env.regs[13] = soc->msram_size;'''

t = t.replace(old, new)
p.write_text(t)
print('Done — SP set to top of RAM after CPU realize')
