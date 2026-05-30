#!/usr/bin/env bash
# Quick TS-only regen via WSL protoc. Use when nanopb (C) is already up to date.
set -euo pipefail
cd "$(dirname "$0")"
PLUGIN="/mnt/f/Constellation/constellation-ui/server/node_modules/.bin/protoc-gen-ts_proto"
OUT="../generated/ts"
rm -rf "$OUT"
mkdir -p "$OUT"
# tsx + ESM bridge requires generated/ts to be a module package; without
# this the named exports in vfd.ts/system.ts/etc. fail to resolve at runtime.
echo '{ "type": "module" }' > "$OUT/package.json"
for p in \
  agristar/common.proto agristar/system.proto agristar/equipment.proto \
  agristar/settings.proto agristar/io.proto agristar/sensors.proto \
  agristar/alarms.proto agristar/logs.proto agristar/accounts.proto \
  agristar/firmware.proto agristar/orbit.proto agristar/vfd.proto \
  agristar/envelope.proto
do
  echo "  [ts] $p"
  protoc --plugin=protoc-gen-ts_proto="$PLUGIN" \
    --ts_proto_out="$OUT" \
    --ts_proto_opt=esModuleInterop=true,outputEncodeMethods=true,outputJsonMethods=true,useExactTypes=false,env=node \
    --proto_path=. "$p"
done
echo "OK"
ls "$OUT/agristar"
