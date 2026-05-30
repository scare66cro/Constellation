#!/usr/bin/env python3
"""Fix constellation_nova.c: add cpu.h include and set SP properly."""
import pathlib

p = pathlib.Path.home() / 'qemu-tm4c/hw/arm/am243x/constellation_nova.c'
t = p.read_text()

# Add cpu.h include if not present
if '"cpu.h"' not in t:
    t = t.replace('#include "hw/arm/boot.h"',
                  '#include "hw/arm/boot.h"\n#include "cpu.h"')
    print('Added cpu.h include')

p.write_text(t)
print('Done')
