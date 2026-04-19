# ═══════════════════════════════════════════════════════════════════════════
# generate.ps1 — Protobuf codegen for Constellation protocol (Windows)
#
# Generates:
#   generated/c/   — nanopb C structs + encode/decode (.pb.h + .pb.c)
#   generated/ts/  — TypeScript interfaces via ts-proto (.ts)
#
# Usage:
#   cd Constellation\proto; .\generate.ps1
# ═══════════════════════════════════════════════════════════════════════════
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProtoDir  = Join-Path $ScriptDir "agristar"
$NanopbOpt = Join-Path $ScriptDir "nanopb"
$OutC      = Join-Path $ScriptDir "..\generated\c"
$OutTS     = Join-Path $ScriptDir "..\generated\ts"
$BridgeDir = Join-Path $ScriptDir "..\constellation-ui\server"

$ProtoFiles = @(
  "agristar/common.proto"
  "agristar/system.proto"
  "agristar/equipment.proto"
  "agristar/settings.proto"
  "agristar/io.proto"
  "agristar/sensors.proto"
  "agristar/alarms.proto"
  "agristar/logs.proto"
  "agristar/accounts.proto"
  "agristar/envelope.proto"
)

Write-Host "=== Constellation Protocol Codegen ===" -ForegroundColor Cyan
Write-Host "Proto dir:  $ProtoDir"
Write-Host "C output:   $OutC"
Write-Host "TS output:  $OutTS"
Write-Host ""

# Clean and create output directories
if (Test-Path $OutC)  { Remove-Item -Recurse -Force $OutC }
if (Test-Path $OutTS) { Remove-Item -Recurse -Force $OutTS }
New-Item -ItemType Directory -Force -Path $OutC  | Out-Null
New-Item -ItemType Directory -Force -Path $OutTS | Out-Null

# ── Generate C code with nanopb ───────────────────────────────────────────
Write-Host "-- Generating C code (nanopb) --" -ForegroundColor Yellow

$nanopbGen = $null
try { $nanopbGen = Get-Command nanopb_generator -ErrorAction SilentlyContinue } catch {}

if ($nanopbGen) {
  foreach ($proto in $ProtoFiles) {
    Write-Host "  [nanopb] $proto"
    & nanopb_generator `
      --proto-path="$ScriptDir" `
      --options-path="$NanopbOpt" `
      --output-dir="$OutC" `
      (Join-Path $ScriptDir $proto)
  }
  Write-Host "  C files generated in $OutC" -ForegroundColor Green
} else {
  Write-Host "  WARNING: nanopb not found. Install: pip install nanopb" -ForegroundColor Red
}

# ── Generate TypeScript code with ts-proto ────────────────────────────────
Write-Host ""
Write-Host "-- Generating TypeScript code (ts-proto) --" -ForegroundColor Yellow

$protoc = $null
try { $protoc = Get-Command protoc -ErrorAction SilentlyContinue } catch {}

if (-not $protoc) {
  Write-Host "  WARNING: protoc not found." -ForegroundColor Red
  Write-Host "  Install: https://github.com/protocolbuffers/protobuf/releases"
  exit 0
}

$tsProtoPlugin = Join-Path $BridgeDir "node_modules\.bin\protoc-gen-ts_proto.cmd"
if (-not (Test-Path $tsProtoPlugin)) {
  $tsProtoPlugin = $null
  try { $tsProtoPlugin = (Get-Command protoc-gen-ts_proto -ErrorAction SilentlyContinue).Path } catch {}
}

if (-not $tsProtoPlugin) {
  Write-Host "  WARNING: ts-proto plugin not found." -ForegroundColor Red
  Write-Host "  Install: cd $BridgeDir; npm install ts-proto"
  exit 0
}

foreach ($proto in $ProtoFiles) {
  Write-Host "  [ts-proto] $proto"
  & protoc `
    --plugin="protoc-gen-ts_proto=$tsProtoPlugin" `
    --ts_proto_out="$OutTS" `
    --ts_proto_opt=esModuleInterop=true `
    --ts_proto_opt=outputEncodeMethods=true `
    --ts_proto_opt=outputJsonMethods=true `
    --ts_proto_opt=useExactTypes=false `
    --ts_proto_opt=env=node `
    --proto_path="$ScriptDir" `
    (Join-Path $ScriptDir $proto)
}

Write-Host "  TypeScript files generated in $OutTS" -ForegroundColor Green
Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
