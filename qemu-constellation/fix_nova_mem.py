#!/usr/bin/env python3
"""Fix constellation_nova.c: replace ATCM+BTCM+MSRAM with flat 2MB RAM at 0x0."""
import pathlib

p = pathlib.Path.home() / 'qemu-tm4c/hw/arm/am243x/constellation_nova.c'
lines = p.read_text().split('\n')

new_mem = [
    '    /*',
    '     * QEMU flat layout: firmware linker script puts everything at 0x0',
    '     * in a contiguous 2 MB region.  Create one big RAM block at 0x0',
    '     * that covers ATCM + code + data + BSS + heap.',
    '     */',
    '    memory_region_init_ram(&s->msram, NULL, "nova.ram",',
    '                           soc->msram_size, &error_fatal);',
    '    memory_region_add_subregion(sys_mem, 0, &s->msram);',
]

# Find the ATCM section start and MSRAM section end
start = None
end = None
for i, line in enumerate(lines):
    if 'ATCM' in line and 'firmware boots' in line:
        start = i
    if 'MSRAM' in line and 'main working' in line:
        for j in range(i, min(i + 4, len(lines))):
            if 'add_subregion' in lines[j]:
                end = j + 1
                break
        break

if start is not None and end is not None:
    print(f'Replacing lines {start}-{end}')
    new_lines = lines[:start] + new_mem + lines[end:]
else:
    print('Could not find memory region boundaries!')
    exit(1)

# Fix the load size
for i, line in enumerate(new_lines):
    if 'AM243X_R5F_ATCM_BASE, AM243X_R5F_ATCM_SIZE' in line:
        new_lines[i] = line.replace(
            'AM243X_R5F_ATCM_BASE, AM243X_R5F_ATCM_SIZE',
            '0, soc->msram_size'
        )
        print(f'Fixed load at line {i}')

p.write_text('\n'.join(new_lines))
print('Done')
