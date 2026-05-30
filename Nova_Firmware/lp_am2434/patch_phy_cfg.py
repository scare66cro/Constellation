#!/usr/bin/env python3
"""
Patch SysCfg-generated DP83869 PHY config in `ti_board_config.c`.

The TI SysCfg template hardcodes `txClkShiftEn = false` (TX delay disabled
on the PHY side) for am243x-LP. Combined with the AM2434's default
`CTRLMMR_ENET1_CTRL.RGMII_ID_MODE = 0` (no SoC-side delay either), the
TX path has zero clock shift -> the link partner sees every frame with
RGMII timing violations and silently drops them as CRC errors.

Symptom: MAC port 1 RX works (PHY rxClkShiftEn=true handles RX), but TX
broadcasts/unicasts never reach the switch port even though
CpswStats.MAC1.txGood is non-zero. See
/memories/repo/lp-am2434-cpsw-tx-debug.md.

Fix: enable TX clock shift on the PHY at 2000 ps (matches the TI EVM
defaults that ship with `enet_layer2_multi_channel/am243x-evm`).
"""
import re
import sys
from pathlib import Path

# NOTE: Driver polarity is INVERTED in dp83869.c::Dp83869_setClkShift —
# `txClkShiftEn=false` SETS the RGMIICTL TXCLKDLY bit (delay ON);
# `txClkShiftEn=true` CLEARS it (delay OFF). The SysCfg default of
# `false` already enables the 2ns delay; flipping it disabled TX. Keep
# the file as generated (no replacements) but leave the script wired
# in so it's easy to re-introduce a hand-tweak if a future diagnosis
# needs it.
REPLACEMENTS = []

def main(path_str: str) -> int:
    path = Path(path_str)
    if not path.is_file():
        print(f"[patch_phy_cfg] ERROR: {path} not found", file=sys.stderr)
        return 1
    text = path.read_text(encoding="utf-8")
    original = text
    changed = []
    for pattern, replacement in REPLACEMENTS:
        new_text, n = re.subn(pattern, replacement, text, count=1)
        if n == 0:
            print(f"[patch_phy_cfg] WARN: pattern not found: {pattern}",
                  file=sys.stderr)
        else:
            changed.append(replacement.strip())
            text = new_text
    if text != original:
        path.write_text(text, encoding="utf-8")
        print(f"[patch_phy_cfg] {path.name} patched: {', '.join(changed)}")
    else:
        print(f"[patch_phy_cfg] {path.name} unchanged (already patched)")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "ti_board_config.c"))
