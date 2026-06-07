// USB Host CDC-ACM front-end: enumerates the flight controller's Virtual COM
// Port and pumps bytes to/from the bridge.
#pragma once

#include <stdbool.h>

// Start the USB host stack and the CDC connect/disconnect handling. Spawns the
// USB library event task and the net->USB TX task. Returns once the host stack
// is installed (device attach happens asynchronously thereafter).
void usb_cdc_host_start(void);

// True while a FC VCP is open and ready to carry traffic.
bool usb_cdc_host_is_connected(void);
