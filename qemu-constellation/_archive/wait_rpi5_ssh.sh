#!/bin/bash
for i in $(seq 1 180); do
  nc -z localhost 2222 2>/dev/null && { echo "SSH_READY at ${i}s"; exit 0; }
  sleep 1
done
echo TIMEOUT_180s
tail -10 /tmp/rpi5_serial.log
