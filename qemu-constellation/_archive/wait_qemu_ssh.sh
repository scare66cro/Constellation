#!/bin/bash
# Wait for QEMU SSH then gather system fingerprint
for i in $(seq 1 30); do
  SSHPASS='4gri*st4r' sshpass -e ssh \
    -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    -o PubkeyAuthentication=no -p 2222 -o ConnectTimeout=3 \
    gellert@localhost "echo QEMU_READY" 2>/dev/null && break
  sleep 2
done
