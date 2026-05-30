#!/usr/bin/env python3
"""
Patch SysCfg-generated `ti_enet_open_close.c` to add accessor functions
that return the CPSW DMA TX/RX channel handles from the static
`gEnetAppSysCfgObj` struct.

Why: the SDK already provides `EnetApp_getTxDmaHandle` / `getRxDmaHandle`
public APIs that return the same handles, BUT they also re-register the
caller's callback as a side effect (see `EnetDma_registerTxEventCb` calls
inside those functions). For OTA-time CPSW DMA pause/resume around
`Flash_write`, we need to look up the handles WITHOUT touching the
callback registration that lwip-port already set at init. So we patch
in two new accessor functions that just read `gEnetAppSysCfgObj.dma.{tx,rx}[i].hCh`.

The struct is `static` so we can't access it from another translation
unit — the only options are (a) remove `static` (fragile, modifies a
public-ish struct definition), or (b) add accessor functions in the same
file that ARE non-static. We pick (b).

Idempotent: re-running the patch on an already-patched file is a no-op.

Constellation-specific. Only invoked from
`Nova_Firmware/lp_am2434/ti-arm-clang/makefile` right after SysConfig
regenerates the file. See `memories/repo/lp-am2434-ospi-runtime-write-failure.md`
once the OTA write path is verified.
"""
import re
import sys
from pathlib import Path

ACCESSOR_BLOCK = """
/* === Constellation patch (patch_enet_accessor.py) ============================
 *  Expose CPSW DMA TX/RX channel handles for OTA-time pause/resume.
 *  Called from `Nova_Firmware/lp_am2434/lp_enet_dma_pause.c`. Read-only —
 *  these functions do NOT touch callbacks, refcounts, or any DMA state.
 *  Returns NULL if the channel was never opened. */
EnetDma_TxChHandle EnetApp_lookupTxCh(uint32_t idx);
EnetDma_RxChHandle EnetApp_lookupRxFlow(uint32_t idx);

EnetDma_TxChHandle EnetApp_lookupTxCh(uint32_t idx)
{
    if (idx >= ENET_ARRAYSIZE(gEnetAppSysCfgObj.dma.tx)) return NULL;
    return gEnetAppSysCfgObj.dma.tx[idx].hTxCh;
}

EnetDma_RxChHandle EnetApp_lookupRxFlow(uint32_t idx)
{
    if (idx >= ENET_ARRAYSIZE(gEnetAppSysCfgObj.dma.rx)) return NULL;
    return gEnetAppSysCfgObj.dma.rx[idx].hRxCh;
}
/* === end Constellation patch ============================================== */
"""

MARKER = "Constellation patch (patch_enet_accessor.py)"


def main(path_str: str) -> int:
    path = Path(path_str)
    if not path.is_file():
        print(f"[patch_enet_accessor] ERROR: {path} not found", file=sys.stderr)
        return 1
    text = path.read_text(encoding="utf-8")
    if MARKER in text:
        print(f"[patch_enet_accessor] {path.name} already patched, skipping")
        return 0
    # Append at end of file — safe since nothing references symbols defined
    # in the patched block from earlier in the file.
    new_text = text.rstrip() + "\n" + ACCESSOR_BLOCK
    path.write_text(new_text, encoding="utf-8")
    print(f"[patch_enet_accessor] {path.name} patched: added EnetApp_lookupTxCh / EnetApp_lookupRxFlow")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: patch_enet_accessor.py <generated/ti_enet_open_close.c>",
              file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
