"""
Bridge Sync Endpoint

This endpoint receives controller data directly from the Constellation bridge server,
bypassing Azure IoT Hub. It mimics the Azure Function's IoTHubTrigger processing.

This enables:
- Local development without Azure IoT Hub
- Future production deployment without Azure dependency
- Simplified architecture (bridge → Django direct)
"""

from django.utils import timezone
from django.utils.dateparse import parse_datetime
from rest_framework import status
from rest_framework.decorators import api_view, permission_classes, authentication_classes
from rest_framework.permissions import AllowAny, IsAuthenticated
from rest_framework.response import Response
from ..models.IoTClient import IoTClient
from ..authentication import IoTClientTokenAuthentication, IsIoTClientAuthenticated


def process_bridge_message(payload: dict, device_id: str) -> dict:
    """
    Process bridge payload into realtime/settings/frontMatter format.
    Mirrors the Azure Function's processMessage() logic.
    """
    # Calculate pile average temperature
    pile_avg = 0
    pile_temps = payload.get('PileTempsData', [])
    if pile_temps and len(pile_temps) > 0:
        valid = [float(t) for i, t in enumerate(pile_temps) if i % 2 == 0 and t != '--']
        if valid:
            pile_avg = sum(valid) / len(valid)

    main_data = payload.get('MainData', [])
    pgm_data = payload.get('PgmData', [])
    basic_setup = payload.get('P2BasicSetupData', [])
    co2_purge = payload.get('Co2PurgeData', [])
    equip_status = payload.get('EquipStatusData', [])

    # Determine CO2 setpoint
    bee_mode = basic_setup[4] == '2' if len(basic_setup) > 4 else False
    purge_mode = co2_purge[0] if co2_purge else '0'
    if purge_mode == '2' and len(co2_purge) > 7:
        co2_setpoint = co2_purge[7] if bee_mode else co2_purge[4]
    else:
        co2_setpoint = '1200'

    realtime = {
        'Type': 'Realtime3' if len(main_data) > 20 else 'Realtime2',
        'StorageID': device_id,
        'UnixTime': int(timezone.now().timestamp() * 1000),
        'AlarmData': payload.get('AlarmData'),
        'MainData': main_data,
        'EquipStatusData': equip_status,
        'CurrentMode': payload.get('CurrentMode'),
        'DailyFanRun': payload.get('DailyFanRun'),
        'TotalFanRun': payload.get('TotalFanRun'),
        'DateTimeData': payload.get('DateTimeData'),
        'PgmData': pgm_data,
        'PileTempsData': pile_temps if len(pile_temps) > 1 else None,
        'PileHumidsData': payload.get('PileHumidsData'),
    }

    settings = {
        'BridgeVersion': payload.get('BridgeVersion', '1.0.0'),
        'Protocol': payload.get('Protocol', 'bridge-direct'),
        'BoardType': payload.get('BoardType'),
        'OutsideAirData': payload.get('OutsideAirData'),
        'AirCureData': payload.get('AirCureData'),
        'FreqCtrlData': payload.get('FreqCtrlData'),
        'RampRateData': payload.get('RampRateData'),
        'HumidCtrlData': payload.get('HumidCtrlData'),
        'Co2PurgeData': co2_purge,
        'MiscData': payload.get('MiscData'),
        'PlenTempDevData': payload.get('PlenTempDevData'),
        'AvailableIoData': payload.get('AvailableIoData'),
        'PgmData': pgm_data,
        'P2BasicSetupData': basic_setup,
        'ControllerList': payload.get('ControllerList'),
        'P2FreshAirData': payload.get('P2FreshAirData'),
        'P2RefrigerationData': payload.get('P2RefrigData'),
        'P2ClimacellData': payload.get('P2ClimaCellData'),
        'P2ServiceData': payload.get('P2ServiceData'),
        'OutputConfigData': payload.get('OutputConfigData'),
        'InputConfigData': payload.get('InputConfigData'),
        'IoNames': payload.get('IoNames'),
        'SysVersion': payload.get('SysVersions'),
        'UserLogSettings': payload.get('UserLogSettings'),
    }

    # Build front_matter for quick dashboard display
    air_cure = payload.get('AirCureData', [])
    plen_temp_dev = payload.get('PlenTempDevData', [])
    load_monitor = payload.get('LoadMonitorData', [])
    sys_versions = payload.get('SysVersions', [])

    front_matter = {
        'main': [
            payload.get('CurrentMode', [''])[0] if payload.get('CurrentMode') else '',  # 0 - currentMode
            basic_setup[1] if len(basic_setup) > 1 else '',  # 1 - TempType
            main_data[0] if main_data else '',  # 2 - plenumTemp
            pgm_data[0] if pgm_data else '',  # 3 - plenumTempSet
            pgm_data[5] if len(pgm_data) > 5 else None,  # 4 - plenumTempSet2
            main_data[1] if len(main_data) > 1 else '',  # 5 - plenumHumid
            pgm_data[1] if len(pgm_data) > 1 else '',  # 6 - plenumHumidSet
            main_data[2] if len(main_data) > 2 else '',  # 7 - outsideTemp
            main_data[4] if len(main_data) > 4 else '',  # 8 - outsideHumid
            main_data[8] if len(main_data) > 8 else '',  # 9 - returnTemp
            main_data[7] if len(main_data) > 7 else '',  # 10 - returnHumid
            air_cure[0] if air_cure else '',  # 11 - CureStartTemp
            air_cure[2] if len(air_cure) > 2 else '',  # 12 - CureStartHumid
            str(pile_avg),  # 13 - PileTempAvg
            main_data[10] if len(main_data) > 10 else '',  # 14 - fanSpeed
            main_data[18] if len(main_data) > 20 else (main_data[11] if len(main_data) > 11 else ''),  # 15 - Cooling Output
            main_data[12] if len(main_data) > 12 else '',  # 16 - Mode
            main_data[9] if len(main_data) > 9 else '',  # 17 - co2Level
            main_data[13] if len(main_data) > 13 else '',  # 18 - BurnerOutput
            equip_status[56] if len(equip_status) > 56 else '',  # 19 - BayLight1
            load_monitor[0] if load_monitor else '',  # 20 - BayLight1Name
            equip_status[58] if len(equip_status) > 58 else '',  # 21 - BayLight2
            load_monitor[1] if len(load_monitor) > 1 else '',  # 22 - BayLight2Name
            basic_setup[4] if len(basic_setup) > 4 else '',  # 23 - SystemMode
            equip_status[8] if len(equip_status) > 8 else '',  # 24 - Cure Output
            equip_status[52] if len(equip_status) > 52 else '',  # 25 - Cure Remote Off
            plen_temp_dev[4] if len(plen_temp_dev) > 4 else '',  # 26 - Cure Temp Low
            plen_temp_dev[5] if len(plen_temp_dev) > 5 else '',  # 27 - Cure Temp High
            air_cure[3] if len(air_cure) > 3 else '',  # 28 - Cure Humid High Limit
            pgm_data[2] if len(pgm_data) > 2 else '',  # 29 - Plenum Humid Reference
            air_cure[1] if len(air_cure) > 1 else '',  # 30 - Cure Humid Reference
            main_data[14] if len(main_data) > 14 else '',  # 31 - Calc Humid
            co2_purge[0] if co2_purge else '',  # 32 - CO2 Purge Mode
            co2_setpoint,  # 33 - CO2 Set Point
            main_data[17] if len(main_data) > 17 else '',  # 34 - Refrigeration Output
        ],
        'misc': [
            payload.get('BoardType', [''])[0] if payload.get('BoardType') else '',  # 0 - BoardType
            basic_setup[0] if basic_setup else '',  # 1 - panelName
            # PWA reads misc[2] as the IoT-client protocol version (gates is200plus,
            # short-poll timeouts, etc). Prefer the explicit IoTClientVersion the
            # bridge stamps; fall back to BridgeVersion for older bridges.
            payload.get('IoTClientVersion') or payload.get('BridgeVersion', '1.0.0'),  # 2 - Version
            sys_versions[0] if sys_versions else '',  # 3 - ControllerVersion
        ],
        'AlarmData': payload.get('AlarmData'),
    }

    return {
        'realtime': realtime,
        'settings': settings,
        'front_matter': front_matter,
    }


@api_view(['POST'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def bridge_sync(request):
    """
    Receive controller data from the Constellation bridge server.
    
    Auth: Token in Authorization header, query param, or body.
    
    Expected payload:
    {
        "token": "ad2a77aca5f3",
        "timestamp": "2026-04-13T15:00:00Z", 
        "payload": {
            "MainData": [...],
            "AlarmData": [...],
            "PgmData": [...],
            ...
        }
    }
    """
    data = request.data
    payload = data.get('payload', {})
    
    # Client is authenticated via IoTClientTokenAuthentication
    client = request.auth

    # Process the payload
    processed = process_bridge_message(payload, str(client.id))

    # Update the IoTClient record
    client.realtime = processed['realtime']
    client.settings = processed['settings']
    client.front_matter = processed['front_matter']
    client.last_log = {'payload': payload, 'timestamp': data.get('timestamp')}
    client.time_stamp = timezone.now()
    client.save(register=False)
    
    # Write IoTLog entry (mirrors Azure Function behavior)
    from ..models.IoTLog import IoTLog
    IoTLog.objects.create(
        iot_client=client,
        time_stamp=client.time_stamp,
        payload=processed['realtime']
    )

    return Response({
        'status': 'ok',
        'deviceId': str(client.id),
        'updatedAt': client.time_stamp.isoformat()
    })


@api_view(['GET'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def bridge_commands(request):
    """
    Fetch pending commands for a controller (message queue).
    Bridge polls this endpoint to get commands from the cloud.
    
    Auth: Token in Authorization header or query param.
    """
    # Client is authenticated via IoTClientTokenAuthentication
    client = request.auth

    # Get pending messages from HttpMessageQueue
    from ..models.http_message_queue import HttpMessageQueue
    pending = HttpMessageQueue.objects.filter(
        iot_client=client,
        is_processed=False  # Not yet processed
    ).order_by('created_at')[:10]

    commands = []
    for msg in pending:
        commands.append({
            'id': msg.id,
            'method': msg.method,
            'payload': msg.payload,
            'createdAt': msg.created_at.isoformat() if msg.created_at else None
        })

    return Response({
        'deviceId': str(client.id),
        'commands': commands
    })


@api_view(['POST'])
@permission_classes([AllowAny])  # AllowAny needed - this validates the token before sync starts
def verify_token(request):
    """
    Verify an access token is valid and return the associated IoTClient info.
    Used by the bridge /config endpoint to validate tokens before enabling sync.
    
    Expected payload:
    {
        "token": "ad2a77aca5f3"
    }
    
    Returns:
    {
        "valid": true,
        "deviceId": "uuid",
        "siteName": "Site Name",
        "panelName": "Panel Name"
    }
    """
    token = request.data.get('token')
    
    if not token:
        return Response(
            {'valid': False, 'error': 'token required'},
            status=status.HTTP_400_BAD_REQUEST
        )
    
    try:
        client = IoTClient.objects.select_related('site').get(token=token)
    except IoTClient.DoesNotExist:
        return Response({
            'valid': False,
            'error': 'Invalid token'
        }, status=status.HTTP_404_NOT_FOUND)
    
    return Response({
        'valid': True,
        'deviceId': str(client.id),
        'siteName': client.site.name if client.site else None,
        'panelName': client.name or client.panel_name,
    })


@api_view(['POST'])
@permission_classes([AllowAny])  # AllowAny - this is how a new device registers
def bridge_register(request):
    """
    Register a new device using its provisioning token.
    This is called once when the device first connects.
    
    Expected payload:
    {
        "token": "ad2a77aca5f3",
        "bridgeVersion": "1.0.0",
        "firmwareVersion": "5.45"
    }
    
    Returns:
    {
        "success": true,
        "deviceId": "uuid",
        "siteName": "Site Name", 
        "panelName": "Panel Name",
        "syncInterval": 30000
    }
    """
    token = request.data.get('token')
    bridge_version = request.data.get('bridgeVersion', 'unknown')
    firmware_version = request.data.get('firmwareVersion', 'unknown')
    
    if not token:
        return Response(
            {'success': False, 'error': 'token required'},
            status=status.HTTP_400_BAD_REQUEST
        )
    
    try:
        client = IoTClient.objects.select_related('site').get(token=token)
    except IoTClient.DoesNotExist:
        return Response({
            'success': False,
            'error': 'Invalid token'
        }, status=status.HTTP_404_NOT_FOUND)
    
    if client.deleted:
        return Response({
            'success': False,
            'error': 'Device is deleted'
        }, status=status.HTTP_403_FORBIDDEN)
    
    # Mark token as spent (device is now registered)
    if not client.token_spent:
        client.token_spent = True
        client.active = True
        client.save(update_fields=['token_spent', 'active'], register=False)
    
    # Log registration event
    client.last_log = {
        'event': 'registration',
        'bridgeVersion': bridge_version,
        'firmwareVersion': firmware_version,
        'timestamp': timezone.now().isoformat()
    }
    client.time_stamp = timezone.now()
    client.save(update_fields=['last_log', 'time_stamp'], register=False)
    
    return Response({
        'success': True,
        'deviceId': str(client.id),
        'siteName': client.site.name if client.site else None,
        'panelName': client.name or client.panel_name,
        'syncInterval': 30000,  # 30 seconds default
    })


@api_view(['POST'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def command_ack(request):
    """
    Acknowledge a command has been processed.
    
    Expected payload:
    {
        "commandId": 123,
        "success": true,
        "result": "DataLoadStatus=true,0"
    }
    
    This marks the HttpMessageQueue entry as processed.
    """
    from ..models.http_message_queue import HttpMessageQueue
    
    command_id = request.data.get('commandId')
    success = request.data.get('success', False)
    result = request.data.get('result', '')
    
    if not command_id:
        return Response(
            {'error': 'commandId required'},
            status=status.HTTP_400_BAD_REQUEST
        )
    
    try:
        msg = HttpMessageQueue.objects.get(id=command_id)
    except HttpMessageQueue.DoesNotExist:
        return Response(
            {'error': 'Command not found'},
            status=status.HTTP_404_NOT_FOUND
        )
    
    # Verify the command belongs to this client
    client = request.auth
    if msg.iot_client_id != client.id:
        return Response(
            {'error': 'Command does not belong to this device'},
            status=status.HTTP_403_FORBIDDEN
        )
    
    # Mark as processed
    msg.is_processed = True
    msg.validation = {
        'success': success,
        'result': result,
        'processedAt': timezone.now().isoformat()
    }
    msg.save()
    
    return Response({
        'status': 'ok',
        'commandId': command_id
    })


@api_view(['GET'])
@permission_classes([IsAuthenticated])
def bridge_data(request, iot_client_id):
    """
    Get live controller data from the most recent bridge sync.
    
    This endpoint returns cached realtime/settings/front_matter data
    that was received from the bridge, bypassing Azure IoT Hub entirely.
    
    Used by the PWA when it needs fresh data but device is connected via bridge.
    
    URL: GET /api/bridge/data/<iot_client_id>/
    
    Returns:
    {
        "realtime": { ... },
        "settings": { ... },
        "front_matter": { ... },
        "last_sync": "2026-04-13T15:00:00Z"
    }
    """
    try:
        client = IoTClient.objects.get(id=iot_client_id)
    except IoTClient.DoesNotExist:
        return Response(
            {'error': 'IoTClient not found'},
            status=status.HTTP_404_NOT_FOUND
        )
    
    # Check if user has permission to access this client
    # For now, check if user can read the client
    if not client.has_object_read_permission(request):
        return Response(
            {'error': 'Permission denied'},
            status=status.HTTP_403_FORBIDDEN
        )
    
    # Check if we have data from bridge sync
    if not client.realtime and not client.front_matter:
        return Response(
            {'error': 'No data available - device may not be syncing'},
            status=status.HTTP_503_SERVICE_UNAVAILABLE
        )
    
    # Check freshness - if data is older than 2 minutes, warn
    stale = False
    if client.time_stamp:
        age_seconds = (timezone.now() - client.time_stamp).total_seconds()
        stale = age_seconds > 120
    
    return Response({
        'realtime': client.realtime,
        'settings': client.settings,
        'front_matter': client.front_matter,
        'last_sync': client.time_stamp.isoformat() if client.time_stamp else None,
        'stale': stale,
    })


@api_view(['GET'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def bridge_upgrade_payload(request, version):
    """
    Download upgrade firmware payload for the bridge.
    
    Auth: Token in Authorization header.
    URL: GET /api/bridge/upgrade/<version>/payload/
    
    Returns: Binary firmware file
    """
    from django.http import HttpResponse
    from ..models import upgrades
    from ..database_storage import DatabaseStorage
    
    try:
        upgrade = upgrades.Upgrade.objects.get(version=version)
    except upgrades.Upgrade.DoesNotExist:
        return Response(
            {'error': 'Upgrade version not found'},
            status=status.HTTP_404_NOT_FOUND
        )
    
    storage = DatabaseStorage()
    try:
        payload = storage.open(upgrade.payload.name, 'rb')
        response = HttpResponse(payload, content_type='application/octet-stream')
        response['Content-Length'] = len(payload)
        response['Content-Disposition'] = f'attachment; filename={upgrade.payload.name}'
        return response
    except Exception as e:
        return Response(
            {'error': f'Failed to read payload: {str(e)}'},
            status=status.HTTP_500_INTERNAL_SERVER_ERROR
        )


@api_view(['GET'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def bridge_upgrade_info(request):
    """
    Get latest upgrade information for the bridge.
    
    Auth: Token in Authorization header.
    URL: GET /api/bridge/upgrade/latest/
    
    Returns:
    {
        "version": "2.0.0.j",
        "description": "...",
        "downloadUrl": "/api/bridge/upgrade/2.0.0.j/payload/"
    }
    """
    from ..models import upgrades
    
    upgrade = upgrades.Upgrade.objects.order_by('-id').first()
    if not upgrade:
        return Response(
            {'error': 'No upgrades available'},
            status=status.HTTP_404_NOT_FOUND
        )
    
    return Response({
        'version': upgrade.version,
        'description': upgrade.description,
        'downloadUrl': f'/api/bridge/upgrade/{upgrade.version}/payload/',
    })


# ── Audit log ingest + query (accountability telemetry) ────────────────────
@api_view(['POST'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def bridge_audit(request):
    """
    Ingest a batch of audit entries from the bridge. Each entry is stored as
    an IoTLog row with payload['kind']='audit' so existing telemetry tooling
    continues to work.

    Body:
      {
        "entries": [
          {"ts": "2026-04-17T10:30:00Z", "kind": "save", "actor": "cloud:alice",
           "slot": null, "level": 2, "route": "basic",
           "detail": {"changed": {"basic[9]": {"before":"0","after":"1"}}},
           "ip": "10.0.0.3"},
          ...
        ]
      }
    """
    from ..models.IoTLog import IoTLog
    client = request.auth
    entries = request.data.get('entries') or []
    if not isinstance(entries, list):
        return Response({'error': 'entries must be a list'}, status=400)

    rows = []
    for e in entries[:500]:  # hard cap per batch
        ts = None
        ts_str = e.get('ts')
        if ts_str:
            try:
                ts = parse_datetime(ts_str)
            except Exception:
                ts = None
        if ts is None:
            ts = timezone.now()

        rows.append(IoTLog(
            iot_client=client,
            time_stamp=ts,
            payload={
                'kind': 'audit',
                'auditKind': e.get('kind', 'save'),
                'actor':     e.get('actor') or 'anonymous',
                'slot':      e.get('slot'),
                'level':     e.get('level', 0),
                'route':     e.get('route'),
                'detail':    e.get('detail'),
                'ip':        e.get('ip'),
            }
        ))
    created = IoTLog.objects.bulk_create(rows) if rows else []
    return Response({'created': len(created)})


@api_view(['GET'])
@authentication_classes([IoTClientTokenAuthentication])
@permission_classes([IsAuthenticated])
def audit_query(request):
    """
    Filter audit IoTLog rows for dashboards / accountability reports.

    Query params:
      device     (uuid)      IoTClient id
      actor      (string)    'cloud:alice', 'factory', local username, ...
      route      (string)    page route saved (basic, pid, ...)
      auditKind  (string)    save | login | logout | login_fail | level_change
      from, to   (iso8601)   time range
      limit      (int, default 200, max 2000)
    """
    from ..models.IoTLog import IoTLog
    qp = request.query_params
    qs = IoTLog.objects.filter(payload__kind='audit')

    device = qp.get('device')
    actor = qp.get('actor')
    route = qp.get('route')
    audit_kind = qp.get('auditKind')
    from_ts = qp.get('from')
    to_ts = qp.get('to')

    try:
        limit = min(int(qp.get('limit') or 200), 2000)
    except ValueError:
        limit = 200

    if device:     qs = qs.filter(iot_client_id=device)
    if actor:      qs = qs.filter(payload__actor=actor)
    if route:      qs = qs.filter(payload__route=route)
    if audit_kind: qs = qs.filter(payload__auditKind=audit_kind)
    if from_ts:
        parsed = parse_datetime(from_ts)
        if parsed: qs = qs.filter(time_stamp__gte=parsed)
    if to_ts:
        parsed = parse_datetime(to_ts)
        if parsed: qs = qs.filter(time_stamp__lte=parsed)

    qs = qs.order_by('-time_stamp')[:limit]
    entries = list(qs)
    return Response({
        'count': len(entries),
        'entries': [
            {
                'ts':        r.time_stamp.isoformat(),
                'deviceId':  str(r.iot_client_id),
                **(r.payload or {}),
            }
            for r in entries
        ],
    })
