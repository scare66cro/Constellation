# Constellation — Nova Enclosure R&D

**Date:** March 29, 2026
**Status:** Research Phase
**Parent:** Constellation-Control-Architecture.md
**Related:** Pulsar-Stepper-Actuator-Design.md

---

## 1. Concept

A custom stainless steel enclosure housing the RPi5 (bridge), Nova (AM2434), and a large capacitive touchscreen — built in-shop and mounted in the control room. The control room location keeps electronics out of the storage air (high humidity, dust, temperature extremes).

**Requirements:**
- Stainless steel construction (304 SS minimum) — matches other Constellation enclosures
- Large touchscreen with capacitive touch for the Svelte UI
- Video via RPi5 micro HDMI port
- Touch feedback via USB (not over HDMI) — clean separation, driver-free on Linux
- Panel-mount display with cutout and bezel, sealed against control room dust
- Internal mounting for RPi5, Nova card, Ethernet switch, and power supply
- Cable entry for Ethernet (to card bus switch and building network), AC mains

---

## 2. Touchscreen Display Options

All options below use **HDMI for video + USB for touch** — two separate cables, no proprietary touch protocol. Touch presents as a standard USB HID input device on Raspberry Pi OS — driver-free, zero configuration.

### 2.1 Shortlist — Bare Panels for Custom Enclosure Mount

| Option | Size | Resolution | Panel | Viewing Angle | Touch | Touch Port | Price | Source |
|---|---|---|---|---|---|---|---|---|
| Waveshare 10.1DP-CAPLCD | 10.1" | 1280×800 | IPS | 178° | 10-pt capacitive | USB | ~$85 | Waveshare |
| Waveshare 10.1inch HDMI LCD (E) | 10.1" | 1024×600 | IPS | 170° | 10-pt capacitive | USB | ~$93 | Waveshare |
| Waveshare 11.6inch HDMI LCD (H) | 11.6" | 1920×1080 | IPS | 178° | 10-pt capacitive | USB | ~$120 | Waveshare |
| **Waveshare 13.3inch HDMI LCD (H)** | **13.3"** | **1920×1080** | **IPS** | **178°** | **10-pt capacitive** | **USB** | **~$160** | **Waveshare** |
| Waveshare 15.6inch HDMI LCD | 15.6" | 1920×1080 | IPS | 178° | 10-pt capacitive | USB | ~$160 | Waveshare |

All panels feature toughened glass and optical bonding (no air gap between glass and LCD — reduces glare, prevents internal condensation).

### 2.2 Recommended — 13.3" HDMI LCD (H)

**Waveshare 13.3inch HDMI LCD (H)** — Pololu #17937

| Spec | Value |
|---|---|
| Size | 13.3" diagonal |
| Resolution | 1920×1080 (Full HD) |
| Panel type | IPS |
| Viewing angle | 178° |
| Touch | 10-point capacitive |
| Touch interface | USB (micro USB connector on panel) |
| Video interface | HDMI (standard HDMI connector on panel) |
| Touch panel | Toughened glass, 6H hardness |
| Audio | 3.5mm jack + 4-pin speaker header |
| OSD | Brightness/contrast adjustment buttons on driver board |
| RPi5 compatibility | Driver-free on Raspberry Pi OS, 10-point touch |
| Price | ~$160 |

**Why 13.3":**
- 1920×1080 gives the Svelte UI full desktop-class layout — no cramped controls
- 178° IPS means no washout when standing off to the side
- Large enough to read across the room, small enough to fit a practical enclosure
- Bare panel (no case) — ideal for shop-built stainless box with custom bezel
- Same price as the 15.6" but more practical enclosure dimensions

### 2.3 Why Not DSI?

The RPi5's DSI port supports Waveshare DSI displays up to 13.3" (1920×1080), and DSI carries touch over the ribbon cable — single cable, no USB. However:

- DSI ribbon cables are fragile and short (typically 6–12")
- The RPi5 must be mounted directly behind the display — limits enclosure layout flexibility
- DSI displays are RPi-only — can't reuse with a PC for bench testing or firmware development
- HDMI + USB is universal, field-replaceable, and uses standard cables

For a production control room panel, HDMI + USB is more robust and serviceable.

---

## 3. RPi5 Display Wiring

```
RPi5                                    Display Panel
──────────────                         ──────────────────
micro HDMI 0 ════ micro HDMI→HDMI ════ HDMI input (video)
USB-A port   ════ USB-A to micro USB ═ micro USB (touch input)

Display power:
  12V DC input on display driver board ◄──── 12V supply or barrel jack adapter
  (some Waveshare panels accept 5V via micro USB — check model)
```

**Cables needed inside enclosure:**
- Micro HDMI to HDMI cable, 1–2 ft (~$8)
- USB-A to micro USB cable, 1–2 ft (~$3)
- 12V power cable for display backlight (from shared supply or separate adapter)

**Touch on Linux — zero config:**
The USB touch controller presents as `/dev/input/eventX`. Raspberry Pi OS, Chromium, and the Svelte UI all recognize it natively. No driver installation, no `xorg.conf` edits. Confirm with `evtest` after plugging in.

---

## 4. Enclosure Design Considerations

### 4.1 Internal Components

| Component | Dimensions (approx) | Notes |
|---|---|---|
| 13.3" display panel | 320mm × 200mm × 5mm | Mounts in front panel cutout |
| Display driver board | 100mm × 60mm × 15mm | Mounts behind display, short LVDS flex to panel |
| Raspberry Pi 5 | 85mm × 56mm × 20mm | Bridge computer, runs Svelte UI |
| Nova card (AM2434) | TBD | Main controller — connected via Ethernet |
| Ethernet switch | 5-port unmanaged, ~100mm × 60mm | Card bus + uplink to building LAN |
| Power supply | 24V DIN-rail or 12V+5V multi-output | Powers RPi5, display, Nova |
| Terminal blocks | Varies | AC mains in, Ethernet breakouts |

### 4.2 Enclosure Sizing Estimate

For a 13.3" display, the front panel cutout is approximately 320mm × 200mm (12.6" × 7.9"). With bezel overlap, wiring space behind, and room for the electronics:

| Dimension | Estimate | Notes |
|---|---|---|
| Width | 16–18" (400–460mm) | Display width + side margins for bezel and cable glands |
| Height | 12–14" (300–360mm) | Display height + top/bottom margins |
| Depth | 4–6" (100–150mm) | Display driver + RPi5 + switch + wiring clearance |

**Material:** 304 stainless steel, 16 gauge (1.5mm). Front panel has rectangular cutout with gasket for display. Rear panel has cable glands for Ethernet, AC mains.

### 4.3 Thermal

The control room is climate-controlled (or at least insulated from storage air). Heat sources:
- RPi5: ~5W under load
- Nova: ~3W (estimated)
- Display backlight: ~5W
- Ethernet switch: ~2W
- **Total: ~15W**

A sealed stainless box with 15W internal load and ~0.3 m² surface area gives roughly 5°C rise above ambient — no fans or vents needed in a control room environment.

### 4.4 Front Panel Layout Concept

```
┌─────────────────────────────────────────────┐
│                                             │
│   ┌───────────────────────────────────┐     │
│   │                                   │     │
│   │                                   │     │
│   │        13.3" Touchscreen          │     │
│   │         1920 × 1080               │     │
│   │                                   │     │
│   │                                   │     │
│   └───────────────────────────────────┘     │
│                                             │
│   [ Power LED ]    [ Status LED ]           │
│                                             │
└─────────────────────────────────────────────┘
        ▲                           ▲
     Bezel frame                Cable glands
    (stainless or               (rear panel)
     brushed finish)
```

---

## 5. Parts List — Display Assembly

| Item | Specification | Source | Qty | Unit | Ext |
|---|---|---|---|---|---|
| Touchscreen | Waveshare 13.3inch HDMI LCD (H), 1920×1080, IPS, capacitive USB touch | Waveshare #17937 | 1 | $160.00 | $160.00 |
| Micro HDMI to HDMI cable | 1.5 ft, slim | Amazon | 1 | $8.00 | $8.00 |
| USB-A to micro USB cable | 1.5 ft, data | Amazon | 1 | $3.00 | $3.00 |
| Display gasket | Neoprene foam, 2mm thick, cut to frame | McMaster | 1 | $5.00 | $5.00 |
| Mounting standoffs | M3 × 10mm, stainless | McMaster | 8 | $0.30 | $2.40 |
| | | | | **Display total:** | **$178.40** |

> Enclosure fabrication, RPi5, Nova, Ethernet switch, and power supply are separate line items — not included above. This section covers only the display and its mounting hardware.

---

## 6. Next Steps

- [ ] Confirm display size preference (10.1" vs 13.3" vs 15.6") after reviewing enclosure space constraints
- [ ] Order one 13.3" Waveshare panel for bench test with RPi5
- [ ] Verify touch works with Svelte UI in Chromium kiosk mode
- [ ] Design front panel cutout dimensions from actual display measurements
- [ ] Prototype enclosure in shop — stainless sheet, bent and welded
- [ ] Determine Nova card physical size and mounting once AM2434 board layout is finalized
