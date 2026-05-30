# RPi5 Screen Blanking, Wi-Fi Access Point & mDNS — Changes

**Date:** March 28, 2026
**Target:** RPi5 at 10.1.2.137
**OS:** Raspberry Pi OS (Debian 12 Bookworm)
**Display:** labwc (Wayland compositor), Chromium in kiosk mode

---

## 1. Problem: Screen Blanking

### Old Behavior

The screen blanking used `wlopm` to turn off the HDMI output after 30 minutes of idle:

```bash
# In /home/gellert/Gellert/scripts/WindowsPrograms.sh (line 49)
swayidle -w timeout 1800 "wlopm --off $output" resume "wlopm --on $output" &
```

**Two problems:**
1. `wlopm --off` kills the HDMI signal entirely. The monitor displays "No Signal" instead of a clean black screen.
2. When the user touches the screen to wake it, the touch event passes through to Chromium. Whatever button or link is behind the blank screen gets pressed. The screen wakes up but the user has already navigated somewhere unintended.

### New Behavior

A Python overlay script draws a fullscreen black window on top of everything. The first touch dismisses the overlay but does NOT pass through to Chromium underneath.

**How it works:**
1. After 30 min idle → `swayidle` launches `/usr/local/bin/screen-blank`
2. The script creates a fullscreen black GTK window on the Wayland **overlay layer** (above all other windows, including kiosk Chromium)
3. The monitor stays on — HDMI signal is active, screen is just black
4. User touches the screen → the overlay catches the touch event, closes itself, and **consumes the event** (returns `True` so it never reaches Chromium)
5. Chromium is revealed in exactly the state it was left in — no accidental button presses

---

## 2. Files Changed

### New File: `/usr/local/bin/screen-blank`

Source copy: `qemu-tm4c/screen-blank.py`

```python
#!/usr/bin/env python3
"""
Black fullscreen overlay for labwc/Wayland.
Uses GtkLayerShell to create an overlay ABOVE all windows (including kiosk Chromium).
First touch/click dismisses the overlay without passing through to windows below.
"""
import gi
import time
gi.require_version('Gtk', '3.0')
gi.require_version('GtkLayerShell', '0.1')
from gi.repository import Gtk, Gdk, GtkLayerShell

IGNORE_SECONDS = 1  # ignore touches for this long after showing

class BlackOverlay(Gtk.Window):
    def __init__(self):
        super().__init__()
        self._show_time = time.monotonic()

        # Use layer shell to guarantee this window is above everything
        GtkLayerShell.init_for_window(self)
        GtkLayerShell.set_layer(self, GtkLayerShell.Layer.OVERLAY)
        GtkLayerShell.set_exclusive_zone(self, -1)  # cover entire screen
        GtkLayerShell.set_keyboard_mode(self, GtkLayerShell.KeyboardMode.ON_DEMAND)

        # Anchor to all edges = fullscreen
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.TOP, True)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.BOTTOM, True)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.LEFT, True)
        GtkLayerShell.set_anchor(self, GtkLayerShell.Edge.RIGHT, True)

        # Black background
        self.override_background_color(
            Gtk.StateFlags.NORMAL, Gdk.RGBA(0, 0, 0, 1)
        )

        # Close on any touch or click
        self.add_events(
            Gdk.EventMask.BUTTON_PRESS_MASK |
            Gdk.EventMask.TOUCH_MASK
        )
        self.connect('button-press-event', self._dismiss)
        self.connect('touch-event', self._dismiss)
        self.connect('key-press-event', self._dismiss)

        self.show_all()

    def _dismiss(self, *args):
        if time.monotonic() - self._show_time < IGNORE_SECONDS:
            return True  # too soon -- swallow but stay visible
        Gtk.main_quit()
        return True  # consume the event -- do not pass through

if __name__ == '__main__':
    BlackOverlay()
    Gtk.main()
```

**Key technical details:**
- `GtkLayerShell.Layer.OVERLAY` — highest Wayland layer, above all windows including kiosk Chromium
- `set_exclusive_zone(-1)` — allows the window to cover the entire screen includingreserved areas
- `return True` in `_dismiss` — this is what swallows the touch. GTK signal handlers  that return `True` consume the event so it doesn't propagate to windows below.

### Modified File: `/home/gellert/Gellert/scripts/WindowsPrograms.sh`

**Line 49 changed from:**
```bash
swayidle -w timeout 1800 "wlopm --off $output" resume "wlopm --on $output" &
```

**To:**
```bash
swayidle -w timeout 1800 "/usr/local/bin/screen-blank" resume "pkill screen-blank" &
```

The `resume` command (`pkill screen-blank`) is a safety net — it kills the overlay if swayidle detects activity before the user touches the overlay directly. In normal use, the user's touch dismisses the overlay via GTK (the `_dismiss` handler), and `pkill` has nothing left to kill.

### Package Installed

```bash
sudo apt-get install -y gir1.2-gtklayershell-0.1
```

This is the GObject introspection bindings for `libgtk-layer-shell0` (which was already installed). Required for the Python script to use `GtkLayerShell`.

---

## 3. Wi-Fi Access Point

### What Was Done

Created a Wi-Fi hotspot on the RPi5's built-in Wi-Fi radio using NetworkManager:

```bash
sudo nmcli device wifi hotspot ifname wlan0 ssid gellert password '4gri*st4r' band bg
sudo nmcli connection modify Hotspot connection.autoconnect yes connection.autoconnect-priority 10
```

### Connection Details

| Setting | Value |
|---|---|
| **SSID** | `constellation` |
| **Password** | `4gri*st4r` |
| **Security** | WPA2-PSK |
| **Band** | 5 GHz (channel 36) |
| **RPi5 AP IP** | `10.42.0.1` |
| **Auto-start** | Yes (survives reboot) |

### How It Works

NetworkManager creates a NAT'd hotspot on `wlan0`. Devices connected to the `constellation` SSID get an IP in the `10.42.0.x` range and can reach the RPi5 at `10.42.0.1`. The hotspot auto-starts on boot.

---

## 4. mDNS (nova.local)

### What Was Done

Set the RPi5 hostname to `nova` so it broadcasts as `nova.local` via mDNS (avahi):

```bash
sudo hostnamectl set-hostname nova
sudo sed -i 's/raspberrypi/nova/' /etc/hosts
sudo systemctl restart avahi-daemon
```

`avahi-daemon` was already installed and active. `avahi-utils` was installed for diagnostics.

### Result

| Access Method | URL |
|---|---|
| **Wired LAN** | `http://nova.local` → resolves to `10.1.2.137` |
| **Wi-Fi AP** | `http://nova.local` → resolves to `10.42.0.1` |

Techs no longer need to remember the IP address. Connect to the `constellation` Wi-Fi or be on the same LAN, then browse to `http://nova.local`.

### Why `nova.local` Works Everywhere (Even with Multiple RPi5s)

Every RPi5 in the Constellation system uses the **same hostname** (`nova`) and the **same SSID** (`constellation`). This is intentional and safe because of how the network is structured:

**Wi-Fi AP path (primary tech access):**
Each RPi5 broadcasts its own `constellation` hotspot on 5 GHz. When a tech connects, their device joins that RPi5's isolated `10.42.0.x` subnet. There is only **one device** on that subnet — the RPi5 itself. So `nova.local` always resolves to `10.42.0.1` with zero ambiguity, regardless of how many other RPi5s exist on the site.

At agricultural sites, buildings are typically **hundreds of feet apart**. The 5 GHz radio on the RPi5 (CYW43455, 20 dBm) has a practical indoor range of ~50-100 feet through walls. A tech standing at a panel will only see the `constellation` SSID from the RPi5 inside that building — neighbouring buildings are well out of range. Even if two panels were in the same building (unlikely), connecting to one AP creates a dedicated subnet where `nova.local` can only resolve to that one Pi.
whil
**The tech workflow is always the same, at every building, on every site:**

1. Stand at the panel
2. Join `constellation` Wi-Fi on phone or laptop
3. Open `http://nova.local`
4. See the Svelte UI for that building's controller

No IPs to remember. No building-specific hostnames. No configuration.

**Wired LAN path (IT / remote management):**
On the wired site LAN, multiple RPi5s share the same subnet. If two or more broadcast `nova.local` via mDNS, avahi detects the collision and silently renames extras to `nova-2.local`, `nova-3.local`, etc. This is unpredictable, so **IT should use static IPs for wired access**, not mDNS. Wired access is for remote management, not for techs at the panel.

### History

The hostname `gellert` was initially chosen but conflicted with the existing PDC (domain controller) at `10.1.2.3` on the site LAN. `nova` was chosen instead — it matches the Constellation card naming.

```
RPi5 Network Interfaces:
    eth0   → site LAN IP (wired, DHCP or static — different per unit)
    wlan0  → 10.42.0.1  (Wi-Fi AP, always the same on every unit)
```

The RPi5 runs dual-homed:
- **eth0** stays connected to the wired LAN — SSH, remote access, cloud sync (IP varies per unit)
- **wlan0** runs as a local access point — techs connect from phones/laptops near the panel (always `10.42.0.1`)

Both interfaces can reach lighttpd on `127.0.0.1`. A tech connected to the `constellation` Wi-Fi opens `http://nova.local` in their browser and sees the full Svelte UI for the building they're standing in.

### Persistence

The `Hotspot` connection profile is stored by NetworkManager and is set to `autoconnect=yes`. It will start automatically on every boot without any changes to startup scripts.

### To Remove

```bash
sudo nmcli connection delete Hotspot
```

---

## 5. Multi-Site Architecture

### The Problem

Large corporate customers may place all RPi5s across multiple physical sites onto a **single flat subnet** via their WAN infrastructure. For example:

- **Penstock** — 8 buildings, 8 RPi5s
- **Eureka** — 8 buildings, 8 RPi5s
- All 16 RPi5s on the same 10.x.x.x subnet, managed centrally

When a Gellert device runs a Network Monitor scan, it sends a UDP broadcast (`255.255.255.255` on the Gellert device query port). **Every** RPi5 on the broadcast domain responds. A panel at Penstock would see all 16 devices — including Eureka's — in its Remote Systems / Network Monitor list.

### Solution: Site-Aware UI Filtering

The golden image stays identical. No network isolation or VLAN configuration is required. Filtering is handled entirely in the Svelte UI:

1. **During commissioning**, set a site tag on each RPi5:
   ```bash
   echo "SITE=penstock" | sudo tee /etc/constellation/site.conf
   ```

2. **Each building's storage name** (set through the Basic Setup page) already identifies the building: `Penstock Bldg 1`, `Penstock Bldg 2`, `Eureka Bldg 1`, etc.

3. **The Network Monitor / Remote Systems page** reads the local site tag and filters the UDP broadcast responses:

   | Filter | Shows |
   |---|---|
   | **My Site** (default) | Only devices matching the local site tag — e.g., Penstock's 8 buildings |
   | **All Sites** | Every device on the subnet — full management view |

   Techs see only their own site by default. A manager can switch to "All Sites" for a cross-site overview.

### Why This Works

- **No firmware changes** — `DeviceCommQueryDevicesLocal()` in GellertServerD already returns every device's storage name, IP, mode, temps, and alarms via UDP broadcast.
- **No network changes** — corporate IT keeps their flat subnet, firewall rules (e.g., outbound to `*.azurewebsites.net` only), and DHCP assignments as-is.
- **No hostname changes** — all RPi5s remain hostname `nova`. The mDNS collision problem on the wired LAN is irrelevant because management uses static IPs, and techs use the Wi-Fi AP (isolated per-building).
- **Golden image stays universal** — the only per-unit customization is the site tag and storage name, both set during commissioning.

### Data Isolation

Each RPi5 only has access to its own ARM firmware via UART. There is no mechanism for one RPi5 to read another's sensor data or control its equipment:

```
RPi5 (Penstock Bldg 1)              RPi5 (Eureka Bldg 1)
    │                                    │
    │ UART (physical wire)               │ UART (physical wire)
    │                                    │
    ▼                                    ▼
  Nova → Orbits/Tritons/Pulsars       Nova → Orbits/Tritons/Pulsars
  (192.168.1.x card bus)              (192.168.1.x card bus)
```

The card bus (192.168.1.x) is behind a UART air gap — not routable, not discoverable from the site LAN. The 192.168.1.x address space exists only on each Nova's Ethernet controller. The Network Monitor scan only shares summary status (mode, temps, alarms), not control access.

### Wi-Fi AP: Unaffected

The `constellation` Wi-Fi AP is NAT'd on `wlan0` (10.42.0.x) and completely isolated from the wired LAN. A tech's phone connected to the hotspot can only reach the local RPi5 at `10.42.0.1`. The flat corporate subnet has no effect on the Wi-Fi workflow.

---

## 6. Testing

### Screen Blanking Test

**To trigger immediately (without waiting 30 min):**

```bash
# SSH into the Pi and launch the overlay manually
ssh gellert@10.1.2.137 "export WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000; screen-blank &"
```

The screen should go black. Touch it — the overlay should dismiss and Chromium should be exactly where it was. No accidental link presses.

**To kill remotely if stuck:**

```bash
ssh gellert@10.1.2.137 "pkill screen-blank"
```

### Wi-Fi AP Test

1. On your phone/laptop, look for SSID `constellation`
2. Connect with password `4gri*st4r`
3. Open `http://nova.local` in a browser
4. You should see the same Svelte UI as `http://10.1.2.137`

---

## 7. Summary of All Changes on 10.1.2.137

| Change | Location | Purpose |
|---|---|---|
| New file | `/usr/local/bin/screen-blank` | Black overlay that swallows first touch |
| Modified | `/home/gellert/Gellert/scripts/WindowsPrograms.sh` line 49 | Use `screen-blank` instead of `wlopm --off` |
| Package | `gir1.2-gtklayershell-0.1` | Python bindings for Wayland layer shell |
| New NM profile | `Hotspot` (NetworkManager) | Wi-Fi AP: SSID `constellation`, 5 GHz, auto-start on boot |
| Hostname | `/etc/hostname`, `/etc/hosts` | Changed from `raspberrypi` to `nova` for mDNS (`nova.local`) |
| Package | `avahi-utils` | mDNS diagnostics |
