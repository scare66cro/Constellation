# qemu-constellation/

Despite the name, this directory **is not about QEMU anymore.** It is the
deployment-asset folder for the rpi5 production target (`gellert@10.47.27.108`).
The QEMU AM2434 machine model that originally lived here is archived under
[`_archive/`](./_archive/) â€” see [`_archive/README.md`](./_archive/README.md).

## What's actually in here

### Bridge deployment (rpi5 hardware)

| File                                       | Purpose                                                                           |
|--------------------------------------------|-----------------------------------------------------------------------------------|
| [`deploy_to_rpi5_hardware.sh`](./deploy_to_rpi5_hardware.sh) | Rsync bridge `server/src/*.ts` + UI `build/` + generated proto bindings to `gellert@10.47.27.108`, install systemd units, restart services. **Run from WSL.** |
| [`deploy_source_to_rpi5.sh`](./deploy_source_to_rpi5.sh)     | Older variant aimed at the QEMU rpi5 image; kept for reference until it's confirmed dead. |
| [`agristar-bridge-hw.service`](./agristar-bridge-hw.service) | systemd unit installed on the rpi5 by `deploy_to_rpi5_hardware.sh`. Runs `npx tsx src/index.ts` against `/dev/ttyAMA0`. |
| [`agristar-pg-backup.sh`](./agristar-pg-backup.sh)           | Daily Postgres backup script (paneldb).                                           |
| [`agristar-pg-backup.service`](./agristar-pg-backup.service) | systemd unit + timer pair for the backup job.                                     |
| [`agristar-pg-backup.timer`](./agristar-pg-backup.timer)     | Timer that triggers the backup service.                                           |

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
| [`rpi5_lighttpd_fix.sh`](./rpi5_lighttpd_fix.sh) / [`rpi5_lighttpd_ws_fix.sh`](./rpi5_lighttpd_ws_fix.sh) | One-off fix scripts; keep until the bug they patch reappears in a fresh image. |
| [`rpi5_kill_legacy_fastcgi.sh`](./rpi5_kill_legacy_fastcgi.sh)       | Removes the legacy `gellertserverd` FastCGI handler from lighttpd.  |
| [`rpi5_run_fix_wrapper.sh`](./rpi5_run_fix_wrapper.sh)               | Convenience wrapper for the kill-legacy script.                    |

### UI deploy helpers

| File                                                       | Purpose                                                                |
|------------------------------------------------------------|------------------------------------------------------------------------|
| [`rpi5_deploy_ui.sh`](./rpi5_deploy_ui.sh)                 | Pushes a SvelteKit `build/` directory to the rpi5 and restarts `uisvelte.service`. |
| [`rpi5_ship_ui_wrapper.sh`](./rpi5_ship_ui_wrapper.sh)     | Wrapper that scp's the deploy script to `/tmp` and executes it.        |
| [`rpi5_chown_ui.sh`](./rpi5_chown_ui.sh)                   | Fixes ownership on `/home/gellert/Gellert/ui-svelte/` after a sudo rsync. |
| [`rpi5_ssh.sh`](./rpi5_ssh.sh)                             | One-line ssh helper with the standard `StrictHostKeyChecking=no` flags. |

## What lived here before (and is now archived)

The `_archive/` directory holds the QEMU AM2434/AM2432 machine models,
rpi5 qcow2 launchers, and ~80 one-shot bench-debug probe scripts from
April 2026. None of them apply to the live LP-board workflow. See the
archive's README if you ever need to revive QEMU emulation.
