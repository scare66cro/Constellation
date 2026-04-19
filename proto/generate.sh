#!/usr/bin/env bash
# ═══════════════════════════════════════════════════════════════════════════
# generate.sh — Protobuf codegen for Constellation protocol
#
# Generates:
#   generated/c/   — nanopb C structs + encode/decode (.pb.h + .pb.c)
#   generated/ts/  — TypeScript interfaces via ts-proto (.ts)
#
# Prerequisites:
#   - protoc          (Protocol Buffers compiler)
#   - nanopb          (pip install nanopb, or from github.com/nanopb/nanopb)
#   - ts-proto        (npm install ts-proto in bridge server)
#
# Usage:
#   cd Constellation/proto && ./generate.sh
# ═══════════════════════════════════════════════════════════════════════════
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROTO_DIR="$SCRIPT_DIR/agristar"
NANOPB_OPTIONS="$SCRIPT_DIR/nanopb"
OUT_C="$SCRIPT_DIR/../generated/c"
OUT_TS="$SCRIPT_DIR/../generated/ts"

# Proto files (order matters for imports)
PROTO_FILES=(
  agristar/common.proto
  agristar/system.proto
  agristar/equipment.proto
  agristar/settings.proto
  agristar/io.proto
  agristar/sensors.proto
  agristar/alarms.proto
  agristar/logs.proto
  agristar/accounts.proto
  agristar/envelope.proto
)

echo "═══ Constellation Protocol Codegen ═══"
echo "Proto dir:  $PROTO_DIR"
echo "C output:   $OUT_C"
echo "TS output:  $OUT_TS"
echo ""

# ── Clean and create output directories ───────────────────────────────────
rm -rf "$OUT_C" "$OUT_TS"
mkdir -p "$OUT_C" "$OUT_TS"

# ── Generate C code with nanopb ───────────────────────────────────────────
echo "── Generating C code (nanopb) ──"

# Find nanopb generator
NANOPB_GEN=""
if command -v nanopb_generator &>/dev/null; then
  NANOPB_GEN="nanopb_generator"
elif command -v python3 -c "import nanopb" &>/dev/null 2>&1; then
  NANOPB_GEN="python3 -m nanopb.generator"
elif [ -n "${NANOPB_DIR:-}" ]; then
  NANOPB_GEN="python3 $NANOPB_DIR/generator/nanopb_generator.py"
else
  echo "WARNING: nanopb not found. Skipping C generation."
  echo "  Install: pip install nanopb"
  echo "  Or set NANOPB_DIR to nanopb source directory"
  NANOPB_GEN=""
fi

if [ -n "$NANOPB_GEN" ]; then
  for proto in "${PROTO_FILES[@]}"; do
    echo "  [nanopb] $proto"
    $NANOPB_GEN \
      --proto-path="$SCRIPT_DIR" \
      --options-path="$NANOPB_OPTIONS" \
      --output-dir="$OUT_C" \
      "$SCRIPT_DIR/$proto"
  done
  echo "  C files generated in $OUT_C"
else
  echo "  Skipped (nanopb not available)"
fi

# ── Generate TypeScript code with ts-proto ────────────────────────────────
echo ""
echo "── Generating TypeScript code (ts-proto) ──"

# Find protoc
if ! command -v protoc &>/dev/null; then
  echo "WARNING: protoc not found. Skipping TypeScript generation."
  echo "  Install: https://github.com/protocolbuffers/protobuf/releases"
  exit 0
fi

# Find ts-proto plugin
TS_PROTO_PLUGIN=""
BRIDGE_DIR="$SCRIPT_DIR/../constellation-ui/server"
if [ -f "$BRIDGE_DIR/node_modules/.bin/protoc-gen-ts_proto" ]; then
  TS_PROTO_PLUGIN="$BRIDGE_DIR/node_modules/.bin/protoc-gen-ts_proto"
elif command -v protoc-gen-ts_proto &>/dev/null; then
  TS_PROTO_PLUGIN="$(command -v protoc-gen-ts_proto)"
else
  echo "WARNING: ts-proto plugin not found. Skipping TypeScript generation."
  echo "  Install: cd $BRIDGE_DIR && npm install ts-proto"
  exit 0
fi

for proto in "${PROTO_FILES[@]}"; do
  echo "  [ts-proto] $proto"
  protoc \
    --plugin="protoc-gen-ts_proto=$TS_PROTO_PLUGIN" \
    --ts_proto_out="$OUT_TS" \
    --ts_proto_opt=esModuleInterop=true \
    --ts_proto_opt=outputEncodeMethods=true \
    --ts_proto_opt=outputJsonMethods=true \
    --ts_proto_opt=useExactTypes=false \
    --ts_proto_opt=env=node \
    --proto_path="$SCRIPT_DIR" \
    "$SCRIPT_DIR/$proto"
done

echo "  TypeScript files generated in $OUT_TS"
echo ""
echo "═══ Done ═══"
