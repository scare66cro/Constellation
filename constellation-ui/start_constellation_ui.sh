#!/bin/bash
###############################################################################
# Constellation UI + Orbit Simulator — Quick Start
#
# Starts:
#   1. Constellation Bridge Server (bridge + Orbit sim) on port 3001
#   2. SvelteKit dev server on port 5173
#
# The bridge server connects to Nova QEMU's UART1 on tcp://localhost:9000.
# Make sure Nova is running first (Constellation.bat or start_constellation.sh).
#
# Usage:
#   bash start_constellation_ui.sh
#
# Stop:  Ctrl+C
###############################################################################

BASE="$(cd "$(dirname "$0")" && pwd)"
SERVER_DIR="$BASE/server"
UI_DIR="$BASE"

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}[✓]${NC} $*"; }
info() { echo -e "${CYAN}[i]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
fail() { echo -e "${RED}[✗]${NC} $*"; }

PIDS=()
CLEANED=0

cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    echo ""
    warn "Shutting down Constellation UI..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null
    done
    sleep 1
    ok "Constellation UI stopped."
    exit 0
}
trap cleanup INT TERM EXIT

echo ""
echo -e "${CYAN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║    Constellation UI + Orbit Simulator               ║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

# ── 1. Bridge Server + Orbit Simulator ────────────────────────────────────
info "Starting Constellation Bridge Server + Orbit Simulator..."
cd "$SERVER_DIR" || { fail "Cannot cd to $SERVER_DIR"; exit 1; }

SERIAL_PORT="tcp://localhost:9000" PORT=3001 \
ORBIT_ID=2 ORBIT_TCP_PORT=5502 ORBIT_API_PORT=9010 \
    npx tsx src/index.ts > /tmp/constellation_bridge.log 2>&1 &
PIDS+=($!)

# Wait for bridge to come up
for i in $(seq 1 30); do
    (echo > /dev/tcp/localhost/3001) 2>/dev/null && break
    sleep 1
done

if (echo > /dev/tcp/localhost/3001) 2>/dev/null; then
    ok "Bridge Server ready (port 3001)"
    ok "Orbit Simulator ready (Modbus TCP :5502, API :9010)"
else
    warn "Bridge Server still starting — check /tmp/constellation_bridge.log"
fi

# ── 2. SvelteKit Dev Server ──────────────────────────────────────────────
echo ""
info "Starting SvelteKit dev server..."
cd "$UI_DIR" || { fail "Cannot cd to $UI_DIR"; exit 1; }

npx vite dev --host 0.0.0.0 --port 5173 > /tmp/constellation_vite.log 2>&1 &
PIDS+=($!)

for i in $(seq 1 20); do
    (echo > /dev/tcp/localhost/5173) 2>/dev/null && break
    sleep 1
done

if (echo > /dev/tcp/localhost/5173) 2>/dev/null; then
    ok "SvelteKit dev server ready"
else
    warn "Vite still starting — check /tmp/constellation_vite.log"
fi

# ── Summary ──────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════${NC}"
echo -e "${GREEN} Constellation UI Running${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════${NC}"
echo ""
echo -e "  UI (dev)         http://localhost:5173"
echo -e "  Bridge Server    http://localhost:3001"
echo -e "  Orbit API        http://localhost:9010/api/status"
echo -e "  Orbit Modbus     tcp://localhost:5502"
echo -e "  Orbit Page       http://localhost:5173/orbit"
echo ""
echo -e "  ${YELLOW}Press Ctrl+C to stop all services${NC}"
echo ""

wait
