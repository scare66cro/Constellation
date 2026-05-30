#!/usr/bin/env python3
"""Fix constellation_orbit.c memory regions: replace ATCM+BTCM+MSRAM with flat RAM."""
import pathlib

p = pathlib.Path.home() / 'qemu-tm4c/hw/arm/am243x/constellation_orbit.c'
lines = p.read_text().split('\n')

# Find ATCM start and MSRAM add_subregion end
start = end = None
for i, line in enumerate(lines):
    if '/* ATCM' in line:
        start = i
    if 'orbit.msram' in line:
        for j in range(i, min(i + 4, len(lines))):
            if 'add_subregion' in lines[j]:
                end = j + 1
                break
        break

if start is not None and end is not None:
    new_mem = [
        '    /*',
        '     * QEMU flat layout: firmware linker puts everything at 0x0.',
        '     */',
        '    memory_region_init_ram(&s->msram, NULL, "orbit.ram",',
        '                           soc->msram_size, &error_fatal);',
        '    memory_region_add_subregion(sys_mem, 0, &s->msram);',
    ]
    lines = lines[:start] + new_mem + lines[end:]
    print(f'Replaced memory regions (lines {start}-{end})')
else:
    print(f'start={start}, end={end} — could not find boundaries!')

p.write_text('\n'.join(lines))
print('Done')
