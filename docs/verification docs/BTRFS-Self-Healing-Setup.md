# BTRFS Self-Healing Setup — RPi5 SSD

**Date:** March 26, 2026
**Applies to:** All Agristar RPi5 units running Ubuntu 22 on NVMe/SATA SSD
**Purpose:** Enable automatic corruption detection and repair on the RPi5 filesystem

---

## 1. Why This Matters

The RPi5 SSD stores:
- Settings backup JSON (Constellation: `/var/lib/agristar/settings_backup.json`)
- History logs (`/var/lib/agristar/history/`)
- Activity logs (`/var/lib/agristar/activity/`)
- Firmware images for upgrade
- The OS and all Agristar services

btrfs already checksums every block (CRC32C), and the existing scrub service detects corruption. But on a **single-copy** filesystem, scrub can only **report** bad blocks — it has no good copy to repair from.

This document adds a second copy so scrub can **automatically fix** corrupt blocks.

---

## 2. Two Options

### Option A: DUP Profile (Simple, No Repartition)

Stores two copies of every data block on the same partition. Can be converted live — no downtime, no reformat.

**Pros:** Simple. One command. No partition changes.
**Cons:** Both copies are on the same partition. Some older kernels (<5.15) don't auto-repair from DUP on scrub. Ubuntu 22 ships 5.15+ so this should work, but RAID1 is more proven.

### Option B: RAID1 Across Two Partitions (Recommended)

Split the SSD into two btrfs devices (two partitions). btrfs mirrors data across both. Scrub auto-repairs from the good copy — this is the fully proven self-healing path.

**Pros:** Guaranteed auto-repair on scrub. btrfs treats partitions as separate devices — better protection against localized block failures.  
**Cons:** Requires creating a second partition (one-time live operation — verified working, no downtime). Uses 2× disk space for data.

> **Note:** btrfs does not have RAID2. The levels are RAID0 (stripe), RAID1 (mirror), RAID1C3 (3 copies), RAID1C4 (4 copies), RAID5/6 (parity — unstable, do not use). RAID1 across two partitions on one SSD is what you want.

---

## 3. Option A — Convert Existing Filesystem to DUP

This can be done live on a running system. No reboot required.

### 3.1 Check Current Profile

```bash
sudo btrfs filesystem df /
```

Typical output on a fresh install:
```
Data, single: total=8.00GiB, used=4.52GiB
System, DUP: total=32.00MiB, used=16.00KiB
Metadata, DUP: total=1.00GiB, used=312.00MiB
```

Metadata is already DUP (Ubuntu default). Data is single — that's what we're fixing.

### 3.2 Convert Data to DUP

```bash
sudo btrfs balance start -dconvert=dup /
```

This rewrites all data blocks with a second copy. Takes a few minutes depending on used space. The filesystem remains online and usable during the balance.

### 3.3 Verify

```bash
sudo btrfs filesystem df /
```

Should now show:
```
Data, DUP: total=10.00GiB, used=4.52GiB
System, DUP: total=32.00MiB, used=16.00KiB
Metadata, DUP: total=1.00GiB, used=312.00MiB
```

All three are now DUP.

### 3.4 Disk Space Impact

DUP doubles the physical space used for data. On a 128 GB SSD with ~10 GB used, you'll use ~20 GB. With 108 GB free, this is irrelevant.

---

## 4. Option B — RAID1 Across Two Partitions (Recommended)

This requires creating a second partition and adding it to the btrfs filesystem, then converting to RAID1.

### 4.1 Current Partition Layout (Typical)

The RPi5 NVMe image typically creates a small root partition that doesn't fill the drive:

```
/dev/nvme0n1p1  ~537MB   vfat   /boot/firmware
/dev/nvme0n1p2  ~15GB    btrfs  /               ← only uses ~15 GB of 128 GB NVMe!
                ~113GB   unallocated
```

### 4.2 Expand Root and Create Second Partition (Live — No Reboot)

**This is done live on the running system.** No offline boot, no USB media needed. Verified working on prototype RPi5 (Kingston 128 GB NVMe, kernel 6.12.25).

```bash
# 1. Check current partition layout
sudo parted /dev/nvme0n1 print
#    Note the end of p2 (likely ~16 GB) and total disk size

# 2. Expand p2 to fill the first half of the drive
#    parted will warn "Partition is being used" — say Yes, it's safe on btrfs
echo "Yes" | sudo parted ---pretend-input-tty /dev/nvme0n1 resizepart 2 64GB

# 3. Expand the btrfs filesystem to fill the enlarged partition
sudo btrfs filesystem resize max /

# 4. Verify — should show ~59 GB total, ~47+ GB free
sudo btrfs filesystem usage / | grep -E "Device size|Free"

# 5. Create p3 in the second half of the drive
sudo parted /dev/nvme0n1 mkpart primary 64GB 100%

# 6. Verify three partitions exist
lsblk /dev/nvme0n1
```

Expected after step 6:
```
nvme0n1      259:0    0 119.2G  0 disk
├─nvme0n1p1  259:1    0   512M  0 part /boot/firmware
├─nvme0n1p2  259:2    0  59.1G  0 part /
└─nvme0n1p3  259:3    0  59.6G  0 part
```

### 4.3 Add Second Partition and Convert to RAID1 (Live)

```bash
# Add p3 as a second btrfs device (live, no format needed — btrfs handles it)
sudo btrfs device add /dev/nvme0n1p3 /

# Convert all data, metadata, and system to RAID1
sudo btrfs balance start -dconvert=raid1 -mconvert=raid1 /

# This takes a few minutes. On the prototype with ~6 GB data it took ~30 seconds (17 chunks).
# Do not close the SSH session. Use screen/tmux if worried about disconnect.

# Verify
sudo btrfs filesystem df /
```

Should show:
```
Data, RAID1: total=6.00GiB, used=5.56GiB
System, RAID1: total=32.00MiB, used=16.00KiB
Metadata, RAID1: total=256.00MiB, used=165.12MiB
```

All three profiles must say **RAID1**.

### 4.4 Update /etc/fstab

No change needed — btrfs auto-assembles multi-device filesystems by UUID. But verify the boot cmdline still references the correct root:

```bash
cat /boot/firmware/cmdline.txt
# Should contain: root=UUID=<your-uuid> rootfstype=btrfs
```

### 4.5 For New Image Builds (Preferred)

If building a fresh RPi5 image for Constellation, partition from the start:

```
/dev/nvme0n1p1  512MB   vfat    /boot/firmware
/dev/nvme0n1p2  50%     btrfs   / (device 1)
/dev/nvme0n1p3  50%     btrfs   / (device 2, RAID1 mirror)
```

Create the filesystem spanning both partitions:

```bash
mkfs.btrfs -m raid1 -d raid1 /dev/nvme0n1p2 /dev/nvme0n1p3
```

Done — RAID1 from day one, no conversion needed.

---

## 5. Scrub Service — Automatic Corruption Detection and Repair

Scrub reads every block, verifies CRC32C checksums, and (with DUP/RAID1) **automatically repairs** bad blocks from the good copy.

### 5.1 Ubuntu Built-in Scrub Timer

Ubuntu RPi5 already ships `btrfs-scrub-root.timer` — it runs weekly. **You do not need to create a custom scrub timer.** Verify it's active:

```bash
systemctl list-timers | grep btrfs
```

If it shows `btrfs-scrub-root.timer`, you're good. If not (unlikely on Ubuntu 22), create one per section 5.2.

### 5.2 Manual Scrub Timer (Only If Ubuntu Doesn't Ship One)

```bash
sudo tee /etc/systemd/system/btrfs-scrub.timer << 'EOF'
[Unit]
Description=Weekly btrfs scrub on root filesystem

[Timer]
OnCalendar=Sun *-*-* 03:00:00
RandomizedDelaySec=3600
Persistent=true

[Install]
WantedBy=timers.target
EOF
```

### 5.3 Override Scrub Service To Add Alert Hook

Whether using Ubuntu's built-in timer or a custom one, override the scrub service to add the alert script (see Section 6):

```bash
sudo tee /etc/systemd/system/btrfs-scrub-root.service << 'EOF'
[Unit]
Description=btrfs scrub on root filesystem
Documentation=man:btrfs-scrub(8)

[Service]
Type=oneshot
ExecStart=/usr/bin/btrfs scrub start -B /
ExecStartPost=/usr/local/bin/btrfs-scrub-alert.sh
IOSchedulingClass=idle
CPUSchedulingPolicy=idle
Nice=19
EOF

sudo systemctl daemon-reload
```

### 5.4 Run a Manual Scrub (Test)

```bash
sudo btrfs scrub start -B /
sudo btrfs scrub status /
```

Output with no errors:
```
UUID:             xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
Scrub started:    Wed Mar 26 14:00:00 2026
Status:           finished
Duration:         0:00:47
Total to scrub:   5.56GiB
Rate:             454.58MiB/s
Error summary:    no errors found
```

Output with a repaired error (RAID1/DUP):
```
Error summary:    csum=1
  Corrected:      1
  Uncorrectable:  0
```

`Corrected: 1` means scrub found a bad block and **automatically repaired it** from the mirror copy.

---

## 6. Scrub Alert Script

Send an alert if scrub finds errors — even corrected ones indicate the SSD may be degrading.

### 6.1 Create the Alert Script

```bash
sudo tee /usr/local/bin/btrfs-scrub-alert.sh << 'SCRIPT'
#!/bin/bash
# Check last scrub result for errors and log/alert

SCRUB_OUTPUT=$(btrfs scrub status / 2>&1)
ERRORS=$(echo "$SCRUB_OUTPUT" | grep -oP 'csum=\K[0-9]+')
UNCORRECTABLE=$(echo "$SCRUB_OUTPUT" | grep -oP 'Uncorrectable:\s+\K[0-9]+')

if [ -n "$ERRORS" ] && [ "$ERRORS" -gt 0 ]; then
    HOSTNAME=$(hostname)
    TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

    MSG="BTRFS SCRUB ALERT on $HOSTNAME at $TIMESTAMP"
    MSG="$MSG\nChecksum errors found: $ERRORS"

    if [ -n "$UNCORRECTABLE" ] && [ "$UNCORRECTABLE" -gt 0 ]; then
        MSG="$MSG\nUNCORRECTABLE ERRORS: $UNCORRECTABLE — DATA LOSS POSSIBLE"
        MSG="$MSG\nACTION REQUIRED: Replace SSD and restore from backup"
        PRIORITY="CRITICAL"
    else
        MSG="$MSG\nAll errors were auto-corrected from mirror copy"
        MSG="$MSG\nSSD may be degrading — monitor closely"
        PRIORITY="WARNING"
    fi

    # Log to syslog (picked up by any log monitoring)
    logger -p local0.warning -t btrfs-scrub "$PRIORITY: $ERRORS checksum errors ($UNCORRECTABLE uncorrectable)"

    # Write to Agristar alert file (for pickup by email/Azure systems)
    echo -e "$MSG" >> /var/lib/agristar/alerts/btrfs_scrub.log

    echo -e "$MSG"
    exit 1
fi

echo "Scrub clean — no errors"
exit 0
SCRIPT

sudo chmod +x /usr/local/bin/btrfs-scrub-alert.sh
```

The alert hook is already wired into the scrub service via `ExecStartPost` in Section 5.3.

> **Shortcut:** The alert script and a setup script that installs everything are available at `qemu-tm4c/btrfs-scrub-alert.sh` and `qemu-tm4c/setup-btrfs-alerts.sh`. SCP them to the unit and run `sudo bash /tmp/setup-btrfs-alerts.sh` instead of doing steps 5.3, 6.1, and 7.2 manually.

---

## 7. Device Stats Monitoring

btrfs tracks per-device error counters independently of scrub. These increment in real-time whenever a read/write error occurs.

### 7.1 Check Device Stats

```bash
sudo btrfs device stats /
```

Clean output (with RAID1 — both devices listed):
```
[/dev/nvme0n1p2].write_io_errs    0
[/dev/nvme0n1p2].read_io_errs     0
[/dev/nvme0n1p2].flush_io_errs    0
[/dev/nvme0n1p2].corruption_errs  0
[/dev/nvme0n1p2].generation_errs  0
[/dev/nvme0n1p3].write_io_errs    0
[/dev/nvme0n1p3].read_io_errs     0
[/dev/nvme0n1p3].flush_io_errs    0
[/dev/nvme0n1p3].corruption_errs  0
[/dev/nvme0n1p3].generation_errs  0
```

Any non-zero value = SSD is having problems.

### 7.2 Device Stats Monitor Service

Check device stats daily (fast — just reads counters, no disk scan):

```bash
sudo tee /etc/systemd/system/btrfs-devstats.timer << 'EOF'
[Unit]
Description=Daily btrfs device stats check

[Timer]
OnCalendar=*-*-* 06:00:00
Persistent=true

[Install]
WantedBy=timers.target
EOF

sudo tee /etc/systemd/system/btrfs-devstats.service << 'EOF'
[Unit]
Description=Check btrfs device error counters

[Service]
Type=oneshot
ExecStart=/bin/bash -c 'ERRS=$(btrfs device stats / | awk "{sum += \\$NF} END {print sum}"); if [ "$ERRS" -gt 0 ]; then logger -p local0.err -t btrfs-devstats "DEVICE ERRORS DETECTED: $ERRS total — check btrfs device stats /"; btrfs device stats / >> /var/lib/agristar/alerts/btrfs_devstats.log; fi'
EOF

sudo systemctl daemon-reload
sudo systemctl enable btrfs-devstats.timer
sudo systemctl start btrfs-devstats.timer
```

---

## 8. Complete Service Summary

| Service | Schedule | What It Does | Runtime |
|---|---|---|---|
| `btrfs-scrub-root.timer` | Weekly (Ubuntu built-in) | Reads every block, verifies checksums, auto-repairs from mirror. Reports errors via alert script. | ~47 seconds on prototype (454 MB/s NVMe) |
| `btrfs-devstats.timer` | Daily (6 AM) | Checks hardware error counters. Instant — no disk scan. Alerts on any non-zero count. | <1 second |

---

## 9. What Happens When Corruption Occurs

### With RAID1 (two partitions) — fully automatic:

```
1. Block on partition 2 develops bit rot
2. Next scrub runs (or next read of that block)
3. btrfs reads block → checksum fails
4. btrfs reads mirror copy from partition 3 → checksum passes
5. btrfs serves the good copy to the application
6. btrfs overwrites the bad block on partition 2 with the good copy
7. Alert script logs the event
8.  → Zero data loss, zero downtime, no manual intervention
```

### With DUP (single partition) — mostly automatic:

```
1. Block develops bit rot
2. Next read of that block (or scrub)
3. btrfs reads first copy → checksum fails
4. btrfs reads second copy → checksum passes
5. btrfs serves the good copy
6. On scrub: repairs the bad copy (kernel 5.15+)
7.  → Zero data loss in most cases
```

### Without DUP/RAID1 (current setup — single copy):

```
1. Block develops bit rot
2. Next scrub detects checksum mismatch
3. btrfs has no second copy → marks block as uncorrectable
4. File is damaged — data loss
5. Alert fires but the damage is done
6.  → Manual recovery needed (restore from Nova QSPI or Azure)
```

---

## 10. Recommendation

**For all units (existing and new): Option B — RAID1.** The live conversion procedure in Section 4 was verified working on the prototype RPi5 (no downtime, no reboot, no USB boot). It takes about 10 minutes per unit over SSH. RAID1 is the proven self-healing path.

```bash
# Summary of the live RAID1 conversion (full steps in Section 4):
echo "Yes" | sudo parted ---pretend-input-tty /dev/nvme0n1 resizepart 2 64GB
sudo btrfs filesystem resize max /
sudo parted /dev/nvme0n1 mkpart primary 64GB 100%
sudo btrfs device add /dev/nvme0n1p3 /
sudo btrfs balance start -dconvert=raid1 -mconvert=raid1 /

# Install alert services (SCP the scripts from qemu-tm4c/ first):
scp qemu-tm4c/btrfs-scrub-alert.sh qemu-tm4c/setup-btrfs-alerts.sh gellert@<unit-ip>:/tmp/
ssh gellert@<unit-ip> "sudo bash /tmp/setup-btrfs-alerts.sh"
```

**Option A (DUP) is still valid** if for some reason you can't repartition (e.g., a unit with no unallocated space). But since every RPi5 image leaves 100+ GB unallocated on the 128 GB NVMe, RAID1 is always possible and always better.

**For new Constellation RPi5 images:** Build with RAID1 from the start — partition the SSD 50/50 and create the filesystem with `mkfs.btrfs -m raid1 -d raid1 /dev/nvme0n1p2 /dev/nvme0n1p3`. No conversion needed.

**Disk space cost:** RAID1 uses 2x physical space for data. On a 128 GB SSD with ~6 GB used, that's ~12 GB mirrored. Over 48 GB remains free (18% used on prototype). The reliability gain far outweighs the space cost.

---








## 11. Field Deployment Guide — RPi5 Units via SSH

> **Verified working:** This exact procedure was run on the prototype RPi5 (10.1.2.137, Kingston 128 GB NVMe, kernel 6.12.25, Ubuntu 22). All commands are copy-paste safe.

**Important clarification:** Your RPi5 units already have COW (copy-on-write). That's built into btrfs — you've had it since the day you formatted the drives. COW is why power outages haven't been corrupting files the way they did on ext4+microSD. What's missing is a **second copy** (RAID1) so that scrub can **repair** corruption instead of just **reporting** it.

This guide converts each unit to RAID1 + installs alert services. Every command runs live over SSH. No reboots. No downtime. No USB boot media. ~10 minutes per unit.

### Step 1: SSH into the unit

```bash
ssh gellert@<unit-ip>
# Or via rpiconnect for field units
```

### Step 2: Check current state

```bash
sudo btrfs filesystem df /
sudo parted /dev/nvme0n1 print
lsblk /dev/nvme0n1
```

You'll likely see:
- `Data, single` — that's what we're fixing
- Partition 2 is only ~15 GB on a 128 GB drive (image doesn't expand to fill NVMe)
- Over 100 GB unallocated

### Step 3: Expand partition 2 to first half of drive

The RPi5 image leaves p2 at ~15 GB. We need room for RAID1. Expand p2 to 64 GB:

```bash
echo "Yes" | sudo parted ---pretend-input-tty /dev/nvme0n1 resizepart 2 64GB
```

parted warns "Partition is being used" — this is safe. btrfs handles live partition expansion.

### Step 4: Expand btrfs to fill the enlarged partition

```bash
sudo btrfs filesystem resize max /
```

Verify:
```bash
sudo btrfs filesystem usage / | grep -E "Device size|Free"
```

Should show ~59 GB total, ~47+ GB free.

### Step 5: Create partition 3

```bash
sudo parted /dev/nvme0n1 mkpart primary 64GB 100%
```

Verify:
```bash
lsblk /dev/nvme0n1
```

Expected:
```
nvme0n1      259:0    0 119.2G  0 disk
├─nvme0n1p1  259:1    0   512M  0 part /boot/firmware
├─nvme0n1p2  259:2    0  59.1G  0 part /
└─nvme0n1p3  259:3    0  59.6G  0 part
```

### Step 6: Add p3 to btrfs and convert to RAID1

```bash
sudo btrfs device add /dev/nvme0n1p3 /
sudo btrfs balance start -dconvert=raid1 -mconvert=raid1 /
```

The balance takes ~30 seconds. Do not close the SSH session. Use `screen` or `tmux` if worried about disconnect.

### Step 7: Verify RAID1

```bash
sudo btrfs filesystem df /
```

All three lines must say **RAID1**:
```
Data, RAID1: total=6.00GiB, used=5.56GiB
System, RAID1: total=32.00MiB, used=16.00KiB
Metadata, RAID1: total=256.00MiB, used=165.12MiB
```

### Step 8: Install alert services (using setup scripts)

From your workstation (where the repo is):

```bash
scp qemu-tm4c/btrfs-scrub-alert.sh gellert@<unit-ip>:/tmp/
scp qemu-tm4c/setup-btrfs-alerts.sh gellert@<unit-ip>:/tmp/
ssh gellert@<unit-ip> "sudo bash /tmp/setup-btrfs-alerts.sh"
```

This installs:
- Alert script at `/usr/local/bin/btrfs-scrub-alert.sh`
- `ExecStartPost` hook on the existing `btrfs-scrub-root.service` (Ubuntu's built-in scrub timer)
- `btrfs-devstats.timer` (daily at 6 AM)
- Alert directory at `/var/lib/agristar/alerts/`

### Step 9: Run a test scrub

```bash
ssh gellert@<unit-ip> "sudo btrfs scrub start -B /; sudo btrfs scrub status /"
```

Look for `Error summary: no errors found`. On the prototype this completed in 47 seconds at 454 MB/s.

### Step 10: Full verification

```bash
ssh gellert@<unit-ip> bash -c '
echo "=== BTRFS Health Check ==="
echo ""
echo "--- Data Profile ---"
sudo btrfs filesystem df /
echo ""
echo "--- Device Stats ---"
sudo btrfs device stats /
echo ""
echo "--- Scrub Status ---"
sudo btrfs scrub status /
echo ""
echo "--- Active Timers ---"
systemctl list-timers | grep btrfs
echo ""
echo "--- Disk Usage ---"
sudo btrfs filesystem usage / | grep -E "Device size|Free|Used"
echo ""
echo "--- Kernel ---"
uname -r
'
```

**Expected:**
- Data/System/Metadata: all RAID1
- Device stats: all zeros (both p2 and p3)
- Scrub: no errors
- Timers: `btrfs-scrub-root.timer` + `btrfs-devstats.timer` listed
- Kernel: 5.15+ (prototype shows 6.12.25)
- Free space: ~48 GB (18% used)

### Done. Repeat for each unit.

After completion, each unit has:

- **COW** — already had it (btrfs default), protects against power-loss half-writes
- **RAID1** — two copies of every block across two partitions, fully proven auto-repair path
- **Weekly scrub** — finds and auto-fixes corrupt blocks from the mirror copy
- **Daily device stats** — catches SSD hardware degradation early
- **Alert logging** — problems get written to `/var/lib/agristar/alerts/` for pickup
