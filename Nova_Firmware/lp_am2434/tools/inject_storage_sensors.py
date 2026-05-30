"""
Phase C bench helper — write test sensor values into STORAGE's
sensor_block via Modbus TCP FC06, then poll back to verify.

NOVA's build_system_status_envelope() reads HR 200..207 from STORAGE
each second:
  HR200 = PT1 temp x10  (e.g. 225 -> 22.5 C)
  HR201 = PT2 temp x10
  HR202 = outside temp x10
  HR203 = return temp x10
  HR204 = outside humid x10
  HR205 = plenum humid x10
  HR206 = return humid x10
  HR207 = CO2 ppm raw

Once these are non-0x7FFF, the Phase C canary in cure_state should
flip from 0xC2 ("all sensors 0x7FFF") to 0xC3 ("at least one float
field populated"). Verify on NOVA's UART by watching `[ORBIT 0] poll`
log lines change from HR=32767 to the values we wrote.
"""
from pymodbus.client import ModbusTcpClient
import sys

HOST = "10.47.27.2"
PORT = 5502

# Address, value, label
WRITES = [
    (200, 225, "PT1   = 22.5 C"),
    (201, 235, "PT2   = 23.5 C"),
    (202, 180, "outsd = 18.0 C"),
    (203, 205, "retn  = 20.5 C"),
    (204, 550, "outsd humid = 55.0 %"),
    (205, 620, "plnm  humid = 62.0 %"),
    (206, 650, "retn  humid = 65.0 %"),
    (207, 420, "CO2 = 420 ppm"),
]

def main() -> int:
    c = ModbusTcpClient(HOST, port=PORT, timeout=3)
    if not c.connect():
        print(f"FAIL: cannot connect to {HOST}:{PORT}")
        return 1
    try:
        for addr, val, label in WRITES:
            r = c.write_register(addr, val, device_id=1)
            ok = (not r.isError()) if hasattr(r, "isError") else True
            print(f"FC06 HR{addr} <- {val:5d}  ({label})  {'OK' if ok else 'ERR ' + str(r)}")

        print()
        print("Read-back HR 200..207:")
        rr = c.read_holding_registers(address=200, count=8, device_id=1)
        if hasattr(rr, "registers"):
            for i, v in enumerate(rr.registers):
                print(f"  HR{200+i} = {v}")
        else:
            print(f"  read failed: {rr}")
    finally:
        c.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
