# qemu-constellation/_archive

QEMU machine-model + rpi5-image launcher scripts from the pre-LP-AM2434
era (when Nova firmware ran on a custom QEMU `am243x` machine and the
rpi5 was emulated via qcow2). Everything in this directory is **dead**
in the current LP-board workflow but is kept here in case the QEMU
emulator approach ever needs to be revived for CI or for porting work.

The current dev/prod path is real silicon end-to-end:

- LP-AM2434 boards flashed via JTAG (`Flash-LP.ps1` + DSS).
- rpi5 at `gellert@10.1.2.108` running the bridge as a real systemd
  service.

See [`docs/Simulator-to-Production-Transition.md`](../../docs/Simulator-to-Production-Transition.md)
for the live architecture.
