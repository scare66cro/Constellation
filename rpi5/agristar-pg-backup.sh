#!/usr/bin/env bash
# Constellation panel — paneldb backup
# =====================================
# Dumps the rpi5 PostgreSQL `paneldb` database (audit_log, user_log,
# activity_log, pid_log, load_log, sensor_log, load_log_bay) to a
# rotating, gzip-compressed snapshot under /var/backups/paneldb/.
#
# Retention policy (mirrors common ops practice):
#   - last 7 daily snapshots               (paneldb-YYYYMMDD-HHMM.sql.gz)
#   - last 4 weekly snapshots  (Sun 02:00) (paneldb-weekly-YYYYMMDD.sql.gz)
#   - last 12 monthly snapshots (1st)      (paneldb-monthly-YYYYMM.sql.gz)
#
# Daily ~10 MB compressed against typical industrial logging volumes,
# so worst-case footprint is ~7*10 + 4*10 + 12*10 ≈ 230 MB. SSD-safe.
#
# Drives by `agristar-pg-backup.timer` (OnCalendar=daily 02:00). To
# trigger manually:  sudo systemctl start agristar-pg-backup.service
#
# To restore a snapshot (DESTRUCTIVE — nukes existing rows):
#   sudo -u postgres dropdb paneldb && sudo -u postgres createdb -O gellert paneldb
#   gunzip -c /var/backups/paneldb/<file>.sql.gz | sudo -u postgres psql paneldb
#
# All output goes to journald via the systemd unit.

set -euo pipefail

BACKUP_DIR="/var/backups/paneldb"
DB_NAME="paneldb"
DB_USER="gellert"
TS="$(date +%Y%m%d-%H%M)"
DOW="$(date +%u)"   # 1..7, Mon..Sun
DOM="$(date +%d)"   # 01..31
MONTH="$(date +%Y%m)"

mkdir -p "$BACKUP_DIR"
chown gellert:gellert "$BACKUP_DIR"
chmod 750 "$BACKUP_DIR"

DAILY_FILE="$BACKUP_DIR/paneldb-$TS.sql.gz"
echo "[pg-backup] daily → $DAILY_FILE"
sudo -u "$DB_USER" pg_dump --format=plain --no-owner --no-privileges "$DB_NAME" \
  | gzip -9 > "$DAILY_FILE.tmp"
mv "$DAILY_FILE.tmp" "$DAILY_FILE"

# Promote to weekly (Sun) and monthly (1st of month) by hardlink so we
# don't waste SSD blocks on duplicate content.
if [[ "$DOW" == "7" ]]; then
  WEEKLY_FILE="$BACKUP_DIR/paneldb-weekly-$(date +%Y%m%d).sql.gz"
  ln -f "$DAILY_FILE" "$WEEKLY_FILE"
  echo "[pg-backup] weekly  → $WEEKLY_FILE"
fi
if [[ "$DOM" == "01" ]]; then
  MONTHLY_FILE="$BACKUP_DIR/paneldb-monthly-$MONTH.sql.gz"
  ln -f "$DAILY_FILE" "$MONTHLY_FILE"
  echo "[pg-backup] monthly → $MONTHLY_FILE"
fi

# Retention prune. -mtime is days; ls-style sort by name keeps the
# date-suffixed files lexically ordered, which matches chronological.
prune() {
  local pattern="$1" keep="$2" label="$3"
  # shellcheck disable=SC2012
  local victims
  victims=$(ls -1 "$BACKUP_DIR"/$pattern 2>/dev/null | sort | head -n -"$keep" || true)
  if [[ -n "$victims" ]]; then
    echo "[pg-backup] pruning $label:"
    echo "$victims" | sed 's/^/  /'
    echo "$victims" | xargs -r rm -f
  fi
}

prune 'paneldb-2[0-9][0-9][0-9][01][0-9][0-3][0-9]-[0-9][0-9][0-9][0-9].sql.gz' 7  'daily'
prune 'paneldb-weekly-*.sql.gz'                                                  4  'weekly'
prune 'paneldb-monthly-*.sql.gz'                                                 12 'monthly'

# Cheap integrity check: gzip listing must succeed on the new file.
if ! gzip -t "$DAILY_FILE" 2>/dev/null; then
  echo "[pg-backup] FATAL: gzip integrity check failed on $DAILY_FILE" >&2
  exit 1
fi

SIZE_KB=$(du -k "$DAILY_FILE" | cut -f1)
echo "[pg-backup] OK   ${SIZE_KB} KB  $(realpath "$DAILY_FILE")"
