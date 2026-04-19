/* Kiosk Service Worker: intercept 503 & offline for auto-recovery */
/* Enhanced with diagnostic logging to identify loopback failures */
const VERSION = 'kiosk-sw-v9';

// Diagnostic logging helpers
const MAX_LOGS = 100;
let diagnosticLogs = [];

function isLoopbackUrl(url) {
  try {
    const u = new URL(url);
    return u.hostname === '127.0.0.1' || u.hostname === 'localhost';
  } catch {
    return false;
  }
}

function logDiagnostic(type, details) {
  const entry = {
    type,
    timestamp: Date.now(),
    isLoopback: isLoopbackUrl(details.url || ''),
    ...details
  };
  diagnosticLogs.push(entry);
  if (diagnosticLogs.length > MAX_LOGS) {
    diagnosticLogs.shift();
  }
  
  // Broadcast to all clients
  self.clients.matchAll().then(clients => {
    clients.forEach(client => {
      client.postMessage({
        type: 'SW_DIAGNOSTIC',
        payload: entry
      });
    });
  });
  
  // Also log to console for debugging
  console.log(`[SW ${type}]`, entry);
}

self.addEventListener('install', (e) => {
  logDiagnostic('INSTALL', { version: VERSION });
  // Skip waiting to activate immediately
  e.waitUntil(self.skipWaiting());
});

self.addEventListener('activate', (e) => {
  logDiagnostic('ACTIVATE', { version: VERSION });
  e.waitUntil((async () => {
    // Clean up old caches
    const keys = await caches.keys();
    await Promise.all(keys.filter(k => k.startsWith('kiosk-sw-')).map(k => caches.delete(k)));
    // Take control of all clients immediately
    await self.clients.claim();
    logDiagnostic('CLAIMED', { version: VERSION });
  })());
});

// Handle messages from clients (e.g., request for diagnostic logs)
self.addEventListener('message', (event) => {
  if (event.data?.type === 'GET_DIAGNOSTICS') {
    event.source.postMessage({
      type: 'SW_DIAGNOSTICS_RESPONSE',
      payload: diagnosticLogs
    });
  } else if (event.data?.type === 'CLEAR_DIAGNOSTICS') {
    diagnosticLogs = [];
    event.source.postMessage({
      type: 'SW_DIAGNOSTICS_CLEARED'
    });
  }
});

// Generate offline fallback HTML
function generateOfflinePage(requestUrl, isLoopback, errorMessage, isNavigation) {
  const title = isLoopback ? 'Local Backend Issue' : 'Connection Issue';
  const message = isLoopback 
    ? 'The local backend is not responding. This may happen during system restarts.' 
    : 'Network connection issue. Waiting for network...';
  
  return `<!DOCTYPE html>
<html>
<head>
  <meta charset='utf-8'>
  <title>${title}</title>
  <meta http-equiv="refresh" content="5">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html, body {
      margin: 0;
      font-family: system-ui, -apple-system, Arial, sans-serif;
      background: #0f0f0f;
      color: #e0e0e0;
      display: flex;
      align-items: center;
      justify-content: center;
      height: 100vh;
      text-align: center;
      padding: 20px;
      box-sizing: border-box;
    }
    .container { max-width: 500px; }
    h1 { font-size: 2em; margin-bottom: 0.5em; color: ${isLoopback ? '#ffa500' : '#ff6b6b'}; }
    p { margin: 0.5em 0; line-height: 1.5; }
    .btn {
      display: inline-block;
      padding: 12px 24px;
      margin: 10px 5px;
      background: #2563eb;
      color: white;
      border: none;
      border-radius: 6px;
      font-size: 1em;
      cursor: pointer;
    }
    .btn:hover { background: #1d4ed8; }
    .btn-secondary { background: #404040; }
    .btn-secondary:hover { background: #505050; }
    .diag {
      font-size: 11px;
      background: #1a1a1a;
      padding: 12px;
      border-radius: 6px;
      margin-top: 20px;
      text-align: left;
      overflow: auto;
      max-height: 150px;
      border: 1px solid #333;
    }
    .diag-label { color: #888; }
    .badge {
      display: inline-block;
      padding: 2px 8px;
      border-radius: 4px;
      font-size: 10px;
      margin-left: 8px;
    }
    .badge-loopback { background: #166534; color: #86efac; }
    .badge-network { background: #1e40af; color: #93c5fd; }
    .status { margin-top: 15px; font-size: 0.9em; color: #888; }
    .spinner {
      display: inline-block;
      width: 16px;
      height: 16px;
      border: 2px solid #444;
      border-top-color: #2563eb;
      border-radius: 50%;
      animation: spin 1s linear infinite;
      vertical-align: middle;
      margin-right: 8px;
    }
    @keyframes spin { to { transform: rotate(360deg); } }
  </style>
</head>
<body>
  <div class="container">
    <h1>${title}</h1>
    <p>${message}</p>
    
    <div style="margin: 20px 0;">
      <button class="btn" onclick="location.reload()">Retry Now</button>
      <button class="btn btn-secondary" onclick="location.href='/'">Go Home</button>
    </div>
    
    <div class="status">
      <span class="spinner"></span>
      Auto-retrying in 5 seconds...
    </div>
    
    <div class="diag">
      <strong>Diagnostic Info:</strong><br><br>
      <span class="diag-label">Mode:</span> 
      ${isLoopback ? '<span class="badge badge-loopback">LOOPBACK</span>' : '<span class="badge badge-network">NETWORK</span>'}<br>
      <span class="diag-label">URL:</span> ${requestUrl}<br>
      <span class="diag-label">Error:</span> ${errorMessage}<br>
      <span class="diag-label">Time:</span> ${new Date().toISOString()}<br>
      <span class="diag-label">SW Version:</span> ${VERSION}<br>
      ${isLoopback ? '<br><em style="color:#ffa500">⚠ Loopback requests should work without internet. Check if backend is running.</em>' : ''}
    </div>
  </div>
  
  <script>
    // Try to reload after 5 seconds
    setTimeout(function() { location.reload(); }, 5000);
    
    // Request diagnostic logs from SW
    if (navigator.serviceWorker && navigator.serviceWorker.controller) {
      navigator.serviceWorker.controller.postMessage({type: 'GET_DIAGNOSTICS'});
    }
  </script>
</body>
</html>`;
}

async function handleRequest(event) {
  const request = event.request;
  const requestUrl = request.url;
  const isLoopback = isLoopbackUrl(requestUrl);
  const isNavigation = request.mode === 'navigate';
  
  try {
    const startTime = Date.now();
    const response = await fetch(request);
    const duration = Date.now() - startTime;
    
    // Handle 503 errors
    if (response.status === 503) {
      logDiagnostic('HTTP_503', { 
        url: requestUrl, 
        isLoopback,
        duration,
        mode: request.mode,
        destination: request.destination
      });
      
      if (isNavigation) {
        return new Response(
          generateOfflinePage(requestUrl, isLoopback, 'HTTP 503 - Service Unavailable', true),
          { status: 503, headers: { 'Content-Type': 'text/html; charset=utf-8' }}
        );
      }
    }
    
    // Log non-OK responses for debugging
    if (!response.ok && isLoopback) {
      logDiagnostic('HTTP_ERROR', { 
        url: requestUrl, 
        status: response.status,
        statusText: response.statusText,
        isLoopback,
        duration
      });
    }
    
    return response;
  } catch (err) {
    const errorMessage = err.message || String(err);
    
    // Log the error
    logDiagnostic('FETCH_ERROR', { 
      url: requestUrl, 
      error: errorMessage,
      isLoopback,
      mode: request.mode,
      destination: request.destination
    });
    
    // Special logging for loopback failures - this is unexpected
    if (isLoopback) {
      logDiagnostic('LOOPBACK_FAILURE', {
        url: requestUrl,
        error: errorMessage,
        critical: true,
        hint: 'Loopback request failed - backend may not be running'
      });
    }
    
    // For navigation requests, return our custom offline page
    if (isNavigation) {
      return new Response(
        generateOfflinePage(requestUrl, isLoopback, errorMessage, true),
        { status: 503, headers: { 'Content-Type': 'text/html; charset=utf-8' }}
      );
    }
    
    // For API/resource requests, throw so the app can handle it
    throw err;
  }
}

// Intercept ALL fetch requests
self.addEventListener('fetch', (event) => {
  const request = event.request;
  const url = request.url;
  
  // Skip chrome-extension and other non-http(s) requests
  if (!url.startsWith('http://') && !url.startsWith('https://')) {
    return;
  }
  
  // Intercept all requests to provide custom error handling
  event.respondWith(handleRequest(event));
});
