# LP-AM2434 OSPI DAC-mode Page Programs — the rule that took 3 days to find

> **tl;dr** — When using OSPI DAC mode for page programs on the
> AM2434 + Cypress S25HL512T, **enable the DAC bit + `IND_AHB_ADDR_TRIGGER_REG`
> lazily on the first write and NEVER disable them between sub-PPs.**
> Toggling DAC off mid-sequence causes the OSPI controller to silently drop
> all AHB writes past the first 5 32-bit words (20 bytes) of every
> sub-PP after the first. The chip still issues PP commands and cycles
> WIP correctly — it just receives zero data — so failures are invisible
> until you read back the chip and compare.

## The rule

In `Nova_Firmware/Platform/hal_flash.c::hal_flash_dac_pp_one` and any
similar new DAC-mode write code path:

```c
uint32_t cfg_pre = hal_rd32(HAL_OSPI_CONFIG_REG);
if ((cfg_pre & HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT) == 0U) {
    hal_wr32(HAL_OSPI_CONFIG_REG, cfg_pre | HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT);
    hal_wr32(HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0x04000000U);
}
// memcpy / word-loop / whatever to dst = 0x60000000 + flash_addr
__asm__ volatile("dsb sy" : : : "memory");
CacheP_wbInv(dst, len, CacheP_TYPE_ALL);
__asm__ volatile("dsb sy" : : : "memory");
// DO NOT clear the DAC bit. DO NOT clear IND_AHB_ADDR_TRIGGER.
```

Reference implementation: TI MCU+ SDK `OSPI_lld_writeDirect`
(`source/drivers/ospi/v0/lld/ospi_v0_lld.c:1615`). It enables both at
the start and never clears either — and that pattern is load-bearing.

## Why this bug is brutal

The failure is *silent* at every layer except a byte-level readback:

| What our code sees | Actual chip state |
|---|---|
| `hal_stig_cmd(0x06)` returns 0 (WREN succeeded) | WEL is set |
| `hal_stig_read(0x05)` shows `sr1 & 0x02 == 0x02` (WEL=1 confirmed) | Correct |
| CPU `memcpy` / word writes to `0x60000000+addr` complete | First 5 words actually reach chip; rest silently dropped by OSPI controller |
| `CacheP_wbInv` returns | Cache lines flushed to OSPI controller, which drops them |
| `hal_wait_wip_clear` returns success (`WIP=0, WEL=0`) | Chip ran a PP, cycled WIP normally; just received no data after byte 20 |
| `hal_flash_write_dac` returns 0 (success) | Page is mostly 0xFF |

You will only catch this with a per-write **readback + compare**.
Without that, you'll think writes succeed and that something
*downstream* (image format, alignment, CRC) is wrong. We chased that
phantom for 3 days.

## The smoking-gun diagnostic

Add a per-chunk verify after every `hal_flash_write_dac`:

```c
volatile uint32_t *cfg_p = (volatile uint32_t *)(uintptr_t)HAL_OSPI_CONFIG_REG;
*cfg_p = (*cfg_p) | (1U << 7);   // ensure DAC enabled for XIP read
CacheP_inv((void *)xip, len, CacheP_TYPE_ALL);
for (uint32_t i = 0; i < len; i++) {
    if (xip[i] != src[i]) {
        // log first_diff position + 16 bytes of context from xip and src
        // ...
        break;
    }
}
```

When the trap is active, you'll see:

```
[OTA]   chip @0x900000+16: 39 6D 4F 3D FF FF FF FF FF FF FF FF FF FF FF FF
[OTA]   src                 : 39 6D 4F 3D FB EE 48 57 11 8A 73 9E 98 2F 7E AB
```

**Exact** `first_diff @20` every time, on every sub-PP-2-onward,
independent of address (Bank B vs scratch region), independent of
power-cycle, independent of OTA image content. That deterministic
20-byte cutoff is the unique signature.

## Why 20 bytes specifically

Speculative — we never confirmed at the silicon level — but the
working theory is that the AM2434 OSPI controller's DAC-write path
has a 5-word in-flight buffer between the AHB slave port and the
chip-protocol generator. When DAC is enabled, that buffer is "primed".
Toggling DAC off flushes whatever was in it and resets the state
machine; the next time DAC is re-enabled (within the same chip-side
WIP cycle, before the chip ends the PP), the controller accepts only
those 5 words before its state machine decides the burst is done and
drops the rest. SDK's pattern (DAC enabled continuously) keeps the
buffer primed for the whole write campaign.

Either way the operational rule is clear: **don't toggle DAC**.

## What we tried that didn't fix it (in case future-you re-tries)

Across firmware iterations 0.A.166 → 0.A.185 we eliminated these:

- Sub-PP size variations (256 → 128 → 64 → 32 bytes — 32 B is a cache-line floor; 16 B triggers cache-allocate hazards that wedge the chip)
- DSB barriers (`dsb sy` between memcpy and `CacheP_wbInv`, and after)
- `HwiP_disable` around the write loop
- `vTaskSuspendAll` (already present from earlier work)
- `lp_enet_dma_pause` (CPSW UDMA TX+RX channels hardware-paused)
- WRITE_COMPLETION_CTRL_REG = 0 (disabling the auto-poll opcode 0xFF, which on Cypress is "Reset to Default I/O Mode")
- CLPEF (cmd 0x30) between retry attempts, both raw STIG and SDK `OSPI_writeCmd` paths
- Bridge-side per-chunk retry with TCP-roundtrip delay between writes
- Bridge-side full-image retry (5 fresh-erase passes; all failed)
- Scratch-region remap (write to fresh chip page on verify mismatch)
- `memcpy` vs explicit `volatile uint32_t` word-store loop
- Switching the chip out of QPI+DTR via `RSTEN+RST` (broke too much else)
- SDK `Flash_write` (INDIRECT_WRITE_XFER) — has a different bug, also broken in this runtime context

The actual fix was a 6-line change: wrap the DAC enable in `if-not-already-on`
and delete the disable at the end.

## Generalised lessons

1. **When two SDK code paths exist (DAC vs INDIRECT) and one works in
   your context, replicate its pattern EXACTLY**, not "approximately".
   We had been following the spirit of `OSPI_writeDirect` for months
   while violating one of its actual invariants.

2. **Add per-write readback + byte-level diff diagnostics early** when
   debugging silent flash failures. Image-level CRC mismatch tells you
   "something is wrong" but takes you days to localise; per-chunk
   byte-diff dumps tell you the failure shape in one bench run.

3. **`hal_wait_wip_clear` returning success does NOT mean your data
   reached the chip.** It means the chip executed *some* PP cycle.
   On Cypress S25HL512T, a PP with zero data bytes (controller dropped
   them all) still cycles WIP cleanly.

4. **The OSPI controller has stateful behavior on the DAC enable/disable
   transition.** Treat the DAC bit as a session, not a per-operation
   toggle.

## References

- `Nova_Firmware/Platform/hal_flash.c::hal_flash_dac_pp_one` — the fix lives here
- `memories/repo/lp-am2434-dac-stochastic-byteloss-and-retry-wedge.md` — full 16-iteration debugging trail (gitignored, but committed in spirit by this doc)
- TI MCU+ SDK `drivers/ospi/v0/lld/ospi_v0_lld.c::OSPI_lld_writeDirect` — the reference implementation we should have matched verbatim
- `docs/firmware-version-current.md` 0.A.186–0.A.187 changelog entries
