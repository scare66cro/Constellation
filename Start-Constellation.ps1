<#
.SYNOPSIS
  Constellation Sensor Injector - panel opener
.DESCRIPTION
  The sensor injector now runs as a systemd service on the rpi5
  (sensor-injector.service, target 10.47.27.2:5502 via the rpi5's
  10.47.27.108/24 alias on eth0). Nothing runs locally on this PC.

  Manage on the rpi5:
    ssh gellert@10.47.27.108 "sudo systemctl status sensor-injector"
    ssh gellert@10.47.27.108 "sudo systemctl restart sensor-injector"
    ssh gellert@10.47.27.108 "journalctl -u sensor-injector -n 50"
#>
$panel = 'http://10.47.27.108:9100/'
Write-Host ""
Write-Host "  Constellation Sensor Injector (rpi5)" -ForegroundColor Cyan
Write-Host "  ====================================" -ForegroundColor Cyan
Write-Host "  Panel : $panel" -ForegroundColor White
Write-Host ""
Start-Process $panel
