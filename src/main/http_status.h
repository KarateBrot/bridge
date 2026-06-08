// Web UI on port 80. Serves a live status page (USB/TCP/WiFi state) and lets the
// user scan for and join a WiFi network; credentials are stored for future use.
//   GET  /        HTML page (status + WiFi join form)
//   GET  /status  JSON snapshot of USB, TCP and WiFi state
//   GET  /scan    JSON list of nearby networks
//   POST /wifi    form-encoded ssid/pass; saves and joins live (empty = forget)
#pragma once

// Start the HTTP server. Call after WiFi is up.
void http_status_start(void);
