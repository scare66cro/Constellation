# Reaching the office Claude Code CLI session from a phone

**Goal:** attach to the *same* Claude Code CLI session running on the
Windows office PC, from a phone, over the existing Tailscale tailnet —
terminal only, persistent across disconnects.

**Why this shape:** `tmux` (which gives the persistent, re-attachable
session) is a Linux tool and does not run in native Windows PowerShell.
The Constellation stack already runs in **WSL (Ubuntu-24.04)**, so the
plan is: run the Claude Code CLI *inside WSL under tmux*, expose that WSL
instance on the tailnet via **Tailscale SSH**, and attach from the phone.

This keeps the laptop screen free — it's a true shared session, not a
mirror of the desktop.

---

## One-time setup (do this at the office PC)

### 1. Make WSL its own Tailscale node with SSH

WSL2 is a separate VM with its own network, so the Windows-host Tailscale
node does not cover it. Give WSL its own node:

```bash
# inside WSL (Ubuntu-24.04)
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up --ssh
```

`--ssh` turns on **Tailscale SSH**: auth is handled by your tailnet
identity, so there are **no SSH keys or passwords to manage**. Approve the
device in the Tailscale admin console; it shows up as e.g. `wsl-ubuntu`.

> Needs `tailscaled` to stay running. Ubuntu-24.04 on a recent WSL build
> has **systemd on by default**, which keeps it alive. Confirm with
> `systemctl status tailscaled`. If systemd is off, enable it: add
> `[boot]\nsystemd=true` to `/etc/wsl.conf`, then `wsl --shutdown` from
> Windows and reopen.

### 2. Install tmux

```bash
sudo apt-get update && sudo apt-get install -y tmux
```

### 3. Install the SSH client on the phone

Any of: **Tailscale's built-in SSH** (tap the node → SSH), **Termius**,
or **Blink** (iOS). Sign the phone into the **same Tailscale account**.

---

## Daily use

### At the PC — start Claude Code inside a named tmux session

```bash
# inside WSL, at the repo
cd /mnt/c/path/to/Constellation     # or your WSL-local clone
tmux new -s work                    # create the persistent session
claude                              # start Claude Code inside it
```

Detach any time with **Ctrl-b then d** — Claude Code keeps running.

### From the phone — attach to that same session

```bash
ssh you@wsl-ubuntu      # Tailscale SSH; use the node's MagicDNS name or 100.x IP
tmux attach -t work     # land in the live Claude Code session
```

You're now typing into the exact session that's running on the office PC.
Detach (Ctrl-b d) or just close the app — it stays alive for next time.

> Convenience: `ssh you@wsl-ubuntu -t 'tmux attach -t work || tmux new -s work'`
> attaches if the session exists, creates it if not — one tap from the phone.

---

## Notes & caveats

- **The stack vs. the CLI.** `Start-Constellation.ps1` (the dev stack:
  QEMU, bridge, UI on `:81`, etc.) is launched from the **Windows** side,
  not WSL. This recipe is only for the *Claude Code CLI* session. To also
  see the **UI** on the phone, reach `http://<pc-tailscale-name>:81` in the
  phone browser (the UI binds `host: true` on port 81).
- **Keep the PC awake.** WSL/tmux survive disconnects, but if Windows
  sleeps, everything pauses. Set the PC to stay awake (or "never sleep on
  AC") if you want all-day phone access.
- **Repo location.** Running Claude Code against `/mnt/c/...` works but is
  slower than a WSL-native clone. For heavy sessions, clone into the WSL
  filesystem (`~/Constellation`) and push/pull through git as usual.
- **Native-Windows Claude Code can't be attached this way.** tmux only
  wraps the WSL process. If you're currently running `claude` in
  PowerShell, switch to running it inside WSL+tmux to make it reachable.
