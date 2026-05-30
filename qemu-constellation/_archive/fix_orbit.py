#!/usr/bin/env python3
"""Fix constellation_orbit.c: same fixes as nova — flat RAM, SP init, cpu.h include."""
import pathlib

p = pathlib.Path.home() / 'qemu-tm4c/hw/arm/am243x/constellation_orbit.c'
lines = p.read_text().split('\n')

# 1. Add cpu.h include
for i, line in enumerate(lines):
    if '"hw/arm/boot.h"' in line and '"cpu.h"' not in '\n'.join(lines):
        lines.insert(i + 1, '#include "cpu.h"')
        print(f'Added cpu.h include at line {i+1}')
        break

# 2. Replace ATCM+BTCM+MSRAM with flat RAM
start = end = None
for i, line in enumerate(lines):
    if 'ATCM' in line and 'firmware boots' in line:
        start = i
    if 'MSRAM' in line and ('main working' in line or 'shared memory' in line):
        for j in range(i, min(i + 4, len(lines))):
            if 'add_subregion' in lines[j]:
                end = j + 1
                break
        break

if start is not None and end is not None:
    new_mem = [
        '    /*',
        '     * QEMU flat layout: firmware linker script puts everything at 0x0',
        '     * in a contiguous region.  Create one big RAM block at 0x0.',
        '     */',
        '    memory_region_init_ram(&s->msram, NULL, "orbit.ram",',
        '                           soc->msram_size, &error_fatal);',
        '    memory_region_add_subregion(sys_mem, 0, &s->msram);',
    ]
    lines = lines[:start] + new_mem + lines[end:]
    print(f'Replaced memory regions (lines {start}-{end})')

# 3. Set SP after CPU realize
new_lines = []
for line in lines:
    new_lines.append(line)
    if 'qdev_realize(DEVICE(s->cpu0), NULL, &error_fatal);' in line:
        new_lines.extend([
            '',
            '    /* Pre-set SP to top of RAM (mimics ROM bootloader) */',
            '    s->cpu0->env.regs[13] = soc->msram_size;',
        ])
        print('Added SP init after CPU realize')

# 4. Fix load_image size
for i, line in enumerate(new_lines):
    if 'AM243X_R5F_ATCM_BASE, AM243X_R5F_ATCM_SIZE' in line:
        new_lines[i] = line.replace(
            'AM243X_R5F_ATCM_BASE, AM243X_R5F_ATCM_SIZE',
            '0, soc->msram_size'
        )
        print(f'Fixed load size at line {i}')

p.write_text('\n'.join(new_lines))
print('Done')
