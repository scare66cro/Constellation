# rpi5/

Deployment-asset folder for the rpi5 production target (`gellert@10.47.27.108`).

Holds the systemd units that run on the rpi5, the deploy scripts that
push code to it from a workstation, the lighttpd front-end config, and
the UART-flash fallback path for reflashing the CONTROLLER LP from the
rpi5 itself.

History: this directory was originally `qemu-constellation/` — a QEMU
machine model for emulating the AM2434/AM2432 silicon during early
bringup. That plan was retired during Phase-C when the real LP boards
came online; the directory was repurposed for rpi5 deployment and
renamed to match its actual purpose 2026-05-30. The QEMU work was
deleted in commit `fd30841` (production phase on LP hardware
confirmed). If you ever need the QEMU machine model files for
historical reference, recover them from `fd30841~1:qemu-constellation/_archive/`.

## What's in here

### Bridge deployment (rpi5 hardware)

| File                                       | Purpose                                                                           |
|--------------------------------------------|-----------------------------------------------------------------------------------|
| [`deploy_to_rpi5_hardware.sh`](./deploy_to_rpi5_hardware.sh) | Rsync bridge `server/src/*.ts` + UI `build/` + generated proto bindings to `gellert@10.47.27.108`, install systemd units, restart services. **Run from WSL.** |
| [`deploy_source_to_rpi5.sh`](./deploy_source_to_rpi5.sh)     | Source-tree deploy variant; useful when you also want the rpi5 to hold a copy of the unbuilt repo for in-place edits. |
| [`agristar-bridge-hw.service`](./agristar-bridge-hw.service) | systemd unit installed on the rpi5 by `deploy_to_rpi5_hardware.sh`. Runs `npx tsx src/index.ts` against `/dev/ttyAMA0`. |
| [`agristar-pg-backup.sh`](./agristar-pg-backup.sh)           | Daily Postgres backup script (paneldb).                                           |
| [`agristar-pg-backup.service`](./agristar-pg-backup.service) | systemd unit for the backup job.                                                  |
| [`agristar-pg-backup.timer`](./agristar-pg-backup.timer)     | Timer that triggers the backup service at 02:15 local + on every boot.            |

### LP-AM2434 OTA reflash from the rpi5

| File                                       | Purpose                                                                           |
|--------------------------------------------|-----------------------------------------------------------------------------------|
| [`flash-lp.sh`](./flash-lp.sh)             | Reflash the CONTROLLER LP from the rpi5 over `/dev/ttyAMA0` (UART boot mode + `uart_uniflash.py`). Fallback path; the primary dev flow is `Flash-LP.ps1` from a Windows PC over JTAG. |
| [`flash_nova_lp_pi.cfg`](./flash_nova_lp_pi.cfg) | UART-flasher cfg consumed by `flash-lp.sh`.                                  |

### lighttpd front-end on the rpi5

| File                                                                 | Purpose                                                            |
|----------------------------------------------------------------------|--------------------------------------------------------------------|
| [`rpi5_lighttpd_clean.conf`](./rpi5_lighttpd_clean.conf)             | Canonical lighttpd config (proxies `/iot/*` and `/proto/stream` to `:9001`). |
| [`rpi5_install_clean_lighttpd.sh`](./rpi5_install_clean_lighttpd.sh) | Installs the above onto the rpi5.                                  |
| [`rpi5_install_wrapper.sh`](./rpi5_install_wrapper.sh)               | Convenience wrapper that pushes + runs the install script from WSL. |
| [`rpi5_lighttpd_fix.sh`](./rpi5_lighttpd_fix.sh) / [`rpi5_lighttpd_ws_fix.sh`](./rpi5_lighttpd_ws_fix.sh) | One-off fix scripts kept for next image-rebuild recurrence.        |
| [`rpi5_kill_legacy_fastcgi.sh`](./rpi5_kill_legacy_fastcgi.sh)       | Removes the legacy `gellertserverd` FastCGI handler from lighttpd.  |
| [`rpi5_run_fix_wrapper.sh`](./rpi5_run_fix_wrapper.sh)               | Convenience wrapper for the kill-legacy script.                    |

### UI deploy helpers

| File                                                       | Purpose                                                                |
|------------------------------------------------------------|------------------------------------------------------------------------|
| [`rpi5_deploy_ui.sh`](./rpi5_deploy_ui.sh)                 | Pushes a SvelteKit `build/` directory to the rpi5 and restarts `uisvelte.service`. |
| [`rpi5_ship_ui_wrapper.sh`](./rpi5_ship_ui_wrapper.sh)     | Wrapper that scp's the deploy script to `/tmp` and executes it.        |
| [`rpi5_chown_ui.sh`](./rpi5_chown_ui.sh)                   | Fixes ownership on `/home/gellert/Gellert/ui-svelte/` after a sudo rsync. |
| [`rpi5_ssh.sh`](./rpi5_ssh.sh)                             | One-line ssh helper with the standard `StrictHostKeyChecking=no` flags. |

## Operational notes

- After this rename landed (2026-05-30), the `Documentation=` URLs in the
  systemd units point at `/home/gellert/Gellert/rpi5/...`. The
  units themselves run from `/usr/local/sbin/` and `/etc/systemd/system/`
  so service operation is unaffected, but **the next time the rpi5 is
  redeployed**, the deploy scripts will install the documentation paths
  matching this new tree.
- Several wrapper scripts (`rpi5_install_wrapper.sh`,
  `rpi5_run_fix_wrapper.sh`, `rpi5_ship_ui_wrapper.sh`) hard-code the
  rpi5 SSH password. Same caveat as the bench helpers documented in
  `.gitignore`: switch to SSH key auth or a `$env:RPI_BENCH_SSHPASS` env
  var before committing any further wrappers of this shape. The
  existing ones predate the rule.
