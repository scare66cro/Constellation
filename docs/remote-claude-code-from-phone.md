# Reaching the office Claude Code CLI session from a phone

**Goal:** get at the Claude Code CLI session running on the **Windows**
office PC, from a phone, over the existing Tailscale tailnet.

**Key fact about this stack:** Claude Code and the Constellation work all
run on **native Windows** — nothing runs in WSL (the old Nova QEMU step is
obsolete). So the clean Linux "`tmux` detach/reattach" trick is **not**
directly available — native Windows has no first-class terminal
multiplexer. The two realistic paths below reflect that.

Either way the connection is **phone → Tailscale → office PC directly**
(Tailscale is already installed on the PC). A cloud/web Claude session
cannot bridge into the tailnet — the setup is done at the PC, once.

---

## Option A — RDP over Tailscale  *(recommended: reliable)*

Your `claude` session keeps running in its Windows console; RDP reconnects
you to that **same logged-in session**, so you see the live terminal exactly
as you left it. Nominally "full desktop," but in practice you just use the
terminal window. Survives disconnects cleanly.

**On the PC (one-time):**
1. Settings → System → **Remote Desktop** → On. (Needs Windows **Pro**;
   Home has no RDP host — use a VNC server like RustDesk/TightVNC instead.)
2. Note the PC's Tailscale name / `100.x.y.z` IP from the Tailscale tray
   icon.

**On the phone:**
3. Install Microsoft's **"Windows App"** (formerly *Remote Desktop*).
4. Add a PC → host = the Tailscale name/IP → sign in with the Windows
   account.

**Daily use:** start (or leave) `claude` running in Windows Terminal at the
PC; from the phone, open Windows App → connect → you're back in the same
live session.

> RDP locks the physical screen while you're connected and logs back in on
> the console session; everything keeps running throughout.

---

## Option B — OpenSSH Server + MSYS2 `tmux`  *(true terminal-only, fiddlier)*

Pure terminal-over-SSH with a real detachable session — at the cost of the
MSYS pseudo-tty quirks that can make a Node TUI misbehave.

**On the PC (one-time):**
1. Enable the OpenSSH server:
   ```powershell
   Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0
   Start-Service sshd; Set-Service sshd -StartupType Automatic
   ```
2. Install **MSYS2** (https://www.msys2.org/), then in an MSYS2 shell:
   ```bash
   pacman -S tmux winpty
   ```
3. Make sure `claude` is on PATH inside the MSYS2 shell (it's a native
   Windows binary; `winpty` gives it a usable TTY).

**Daily use at the PC** — start Claude Code inside a named tmux session
(launched from MSYS2):
```bash
tmux new -s work
winpty claude        # winpty fixes TTY handling for the Node TUI
```
Detach with **Ctrl-b** then **d**.

**From the phone** — SSH client (Tailscale's built-in SSH, Termius, Blink):
```bash
ssh you@<pc-tailscale-name>
# then, in the MSYS2 login shell:
tmux attach -t work
```

> Caveats: the OpenSSH default shell is PowerShell — either launch MSYS2's
> bash from it (`C:\msys64\usr\bin\bash -lc 'tmux attach -t work'`) or set
> the SSH default shell to MSYS2 bash. If the TUI renders garbled, the
> `winpty` wrapper (and a UTF-8 terminal on the phone) is the usual fix.
> This is the flakier path — if it fights you, fall back to Option A.

---

## Choosing

- **Want it to just work** → Option A (RDP). Most reliable on Windows.
- **Want pure terminal-over-SSH** and willing to tolerate MSYS quirks →
  Option B.
- There is no clean native-Windows `tmux`-only path; WSL would give one,
  but Claude Code here runs on Windows, not in WSL.

## Also useful: the UI on the phone

Independent of the above, the Constellation **UI** binds `host: true` on
**port 81**, so once `Start-Constellation.ps1` is up you can open
`http://<pc-tailscale-name>:81` in the phone browser. It's the real,
read/write panel — your existing Level 0/1/2 auth is the guard.

## Keep the PC awake

The Windows console session survives disconnects, but not the machine
**sleeping**. Set "never sleep on AC" if you want all-day access.
