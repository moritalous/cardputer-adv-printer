#include "usb_printer.h"

#include <Arduino.h>
#include <string.h>
#include "usb/usb_host.h"

static bool s_started = false;
static usb_host_client_handle_t s_client = nullptr;
static usb_device_handle_t s_dev = nullptr;
static bool s_claimed = false;
static bool s_inited = false;
static uint8_t s_ifNum = 0;
static uint8_t s_epOut = 0;
static uint8_t s_epIn = 0;
static usb_transfer_t* s_xfer = nullptr;
static usb_transfer_t* s_ctrl = nullptr;
static usb_transfer_t* s_in = nullptr;

static volatile uint8_t s_pendingAddr = 0;
static volatile bool s_devLost = false;
static volatile bool s_xferDone = false;
static volatile bool s_ctrlDone = false;
static volatile bool s_inNeedResubmit = false;

static char s_status[64] = "USB host not started";

static const size_t XFER_BUF_SIZE = 4096;

static void setStatus(const char* s) {
  strlcpy(s_status, s, sizeof(s_status));
}

static void clientCb(const usb_host_client_event_msg_t* msg, void*) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    s_pendingAddr = msg->new_dev.address;
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    s_devLost = true;
  }
}

static void xferCb(usb_transfer_t*) {
  s_xferDone = true;
}

static void closeDevice() {
  if (!s_dev) return;
  if (s_claimed) {
    usb_host_interface_release(s_client, s_dev, s_ifNum);
    s_claimed = false;
  }
  usb_host_device_close(s_client, s_dev);
  s_dev = nullptr;
  s_inited = false;
  s_inNeedResubmit = false;
}

// Claims the first class-7 (printer) interface and its bulk OUT endpoint.
// Strictly class 7: matching any bulk OUT device would send print jobs into
// e.g. a USB flash drive.
static void tryClaim(uint8_t addr) {
  if (usb_host_device_open(s_client, addr, &s_dev) != ESP_OK) {
    s_dev = nullptr;
    setStatus("device open failed");
    return;
  }

  const usb_config_desc_t* cd;
  if (usb_host_get_active_config_descriptor(s_dev, &cd) != ESP_OK) {
    closeDevice();
    setStatus("no config descriptor");
    return;
  }

  const uint8_t* p = (const uint8_t*)cd;
  const int total = cd->wTotalLength;
  bool inPrinterIf = false;
  uint8_t curIf = 0, curAlt = 0, otherClass = 0;
  bool found = false;
  uint8_t foundIf = 0, foundAlt = 0, foundEp = 0;

  for (int i = 0; i + 1 < total && p[i] > 0; i += p[i]) {
    if (p[i + 1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      inPrinterIf = (p[i + 5] == 0x07);
      if (inPrinterIf) {
        curIf = p[i + 2];
        curAlt = p[i + 3];
      } else {
        otherClass = p[i + 5];
      }
    } else if (inPrinterIf && p[i + 1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
      const uint8_t ea = p[i + 2];
      if (!found && !(ea & 0x80) && (p[i + 3] & 0x03) == 2) {  // OUT + bulk
        foundIf = curIf;
        foundAlt = curAlt;
        foundEp = ea;
        found = true;
      }
      if ((ea & 0x80) && (p[i + 3] & 0x03) == 2) {  // IN + bulk
        s_epIn = ea;
      }
    }
  }

  if (!found) {
    closeDevice();
    char buf[64];
    snprintf(buf, sizeof(buf), "not a printer (class %02X)", otherClass);
    setStatus(buf);
    return;
  }

  if (usb_host_interface_claim(s_client, s_dev, foundIf, foundAlt) != ESP_OK) {
    closeDevice();
    setStatus("interface claim failed");
    return;
  }

  s_ifNum = foundIf;
  s_epOut = foundEp;
  s_claimed = true;
  setStatus("printer ready");
}

bool usbPrinterStart() {
  if (s_started) return true;

  const usb_host_config_t hostCfg = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  if (usb_host_install(&hostCfg) != ESP_OK) {
    setStatus("usb_host_install failed");
    return false;
  }

  const usb_host_client_config_t clientCfg = {
    .is_synchronous = false,
    .max_num_event_msg = 8,
    .async = { .client_event_callback = clientCb, .callback_arg = nullptr },
  };
  if (usb_host_client_register(&clientCfg, &s_client) != ESP_OK) {
    setStatus("client register failed");
    return false;
  }

  if (usb_host_transfer_alloc(XFER_BUF_SIZE, 0, &s_xfer) != ESP_OK) {
    setStatus("transfer alloc failed");
    return false;
  }

  s_started = true;
  setStatus("waiting for printer");
  return true;
}

bool usbPrinterStarted() {
  return s_started;
}

void usbPrinterPump() {
  if (!s_started) return;

  uint32_t flags;
  usb_host_lib_handle_events(0, &flags);
  usb_host_client_handle_events(s_client, 0);

  if (s_devLost) {
    s_devLost = false;
    closeDevice();
    setStatus("printer disconnected");
  }
  if (s_pendingAddr) {
    const uint8_t addr = s_pendingAddr;
    s_pendingAddr = 0;
    if (!s_dev) tryClaim(addr);
  }
  if (s_inNeedResubmit && s_claimed && s_in) {
    s_inNeedResubmit = false;
    usb_host_transfer_submit(s_in);
  }
}

// --- connect-time handshake -------------------------------------------------
// A freshly powered M220 silently ignores raster jobs until the host has
// issued printer-class requests at least once (usbprint.sys does this at
// attach, which is why the printer "just works" on a PC). The state persists
// until the printer powers off. Also keep the bulk IN endpoint drained: the
// printer reports status bytes after each job. Do not remove any of this.

static void ctrlCb(usb_transfer_t*) {
  s_ctrlDone = true;
}

static bool ctrlIn(uint8_t bReq, uint16_t wLength) {
  if (!s_ctrl && usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 512, 0, &s_ctrl) != ESP_OK) {
    return false;
  }
  usb_setup_packet_t* setup = (usb_setup_packet_t*)s_ctrl->data_buffer;
  setup->bmRequestType = 0xA1;  // class request, interface, device-to-host
  setup->bRequest = bReq;
  setup->wValue = 0;
  setup->wIndex = (uint16_t)(s_ifNum << 8);
  setup->wLength = wLength;

  s_ctrl->num_bytes = sizeof(usb_setup_packet_t) + wLength;
  s_ctrl->device_handle = s_dev;
  s_ctrl->bEndpointAddress = 0;
  s_ctrl->callback = ctrlCb;
  s_ctrl->context = nullptr;
  s_ctrlDone = false;

  if (usb_host_transfer_submit_control(s_client, s_ctrl) != ESP_OK) return false;

  const uint32_t startMs = millis();
  while (!s_ctrlDone) {
    usbPrinterPump();
    if (!s_dev || millis() - startMs > 3000) return false;
    delay(1);
  }
  return s_ctrl->status == USB_TRANSFER_STATUS_COMPLETED;
}

static void inDrainCb(usb_transfer_t* t) {
  // Read and discard; the point is that the IN pipe never backs up.
  s_inNeedResubmit = (t->status == USB_TRANSFER_STATUS_COMPLETED);
}

static void startInDrain() {
  if (!s_epIn) return;
  if (!s_in && usb_host_transfer_alloc(64, 0, &s_in) != ESP_OK) return;
  s_in->num_bytes = 64;
  s_in->device_handle = s_dev;
  s_in->bEndpointAddress = s_epIn;
  s_in->callback = inDrainCb;
  s_in->context = nullptr;
  usb_host_transfer_submit(s_in);
}

// Runs from top level (WaitReady/Write), never from inside the pump: the
// control transfers wait by pumping, which must not recurse.
static void ensureInited() {
  if (s_inited || !s_claimed) return;
  ctrlIn(0x00, 256);  // GET_DEVICE_ID -- result unused, the request matters
  ctrlIn(0x01, 1);    // GET_PORT_STATUS
  startInDrain();
  s_inited = true;
}

bool usbPrinterWaitReady(uint32_t timeoutMs) {
  const uint32_t startMs = millis();
  while (!s_claimed) {
    if (millis() - startMs > timeoutMs) return false;
    usbPrinterPump();
    delay(10);
  }
  ensureInited();
  return true;
}

bool usbPrinterWrite(const uint8_t* data, size_t len) {
  if (!s_claimed) return false;
  ensureInited();

  size_t off = 0;
  while (off < len) {
    const size_t n = min(len - off, XFER_BUF_SIZE);
    memcpy(s_xfer->data_buffer, data + off, n);
    s_xfer->num_bytes = (int)n;
    s_xfer->device_handle = s_dev;
    s_xfer->bEndpointAddress = s_epOut;
    s_xfer->callback = xferCb;
    s_xfer->context = nullptr;
    s_xferDone = false;

    if (usb_host_transfer_submit(s_xfer) != ESP_OK) {
      setStatus("transfer submit failed");
      return false;
    }

    // Completion callbacks fire inside the pump, so keep pumping. A full
    // printer buffer just NAKs -- that is the flow control.
    const uint32_t startMs = millis();
    while (!s_xferDone) {
      usbPrinterPump();
      if (!s_claimed) return false;
      if (millis() - startMs > 30000) {
        setStatus("transfer timeout");
        return false;
      }
      delay(1);
    }

    if (s_xfer->status != USB_TRANSFER_STATUS_COMPLETED ||
        s_xfer->actual_num_bytes != (int)n) {
      setStatus("transfer failed");
      return false;
    }
    off += n;
  }
  return true;
}

const char* usbPrinterStatus() {
  return s_status;
}
