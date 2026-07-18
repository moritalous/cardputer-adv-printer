#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal USB printer-class (interface class 0x07) host driver on the
// ESP-IDF usb_host stack. Single device, single bulk OUT pipe.
//
// usbPrinterStart() switches the ESP32-S3's only USB PHY from Serial/JTAG to
// OTG host: no serial and no flashing until a power cycle. Call it as late
// as possible (first print).

bool usbPrinterStart();
bool usbPrinterStarted();

// Processes pending USB events. Cheap no-op before start; call from loop().
void usbPrinterPump();

// Pumps until a printer-class device is attached and claimed.
bool usbPrinterWaitReady(uint32_t timeoutMs);

// Blocking bulk OUT write (chunked internally).
bool usbPrinterWrite(const uint8_t* data, size_t len);

// Short state string for the display (serial is unavailable in host mode).
const char* usbPrinterStatus();
