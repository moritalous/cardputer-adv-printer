#include <M5Cardputer.h>
#include "usb/usb_host.h"

// USB descriptor viewer for identifying the printer. Results go to the
// display: serial dies the moment the USB host starts (shared PHY), so they
// cannot be harvested over serial.
// Host start is gated on a key press so reflashing works until then.

static usb_host_client_handle_t client;
static volatile uint8_t newDevAddr = 0;
static volatile bool devGone = false;
static bool started = false;

static void clientCb(const usb_host_client_event_msg_t* msg, void*) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    newDevAddr = msg->new_dev.address;
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    devGone = true;
  }
}

static const char* epTypeName(uint8_t attr) {
  switch (attr & 0x03) {
    case 0: return "ctrl";
    case 1: return "isoc";
    case 2: return "bulk";
    default: return "intr";
  }
}

static void showDevice(uint8_t addr) {
  usb_device_handle_t dev;
  if (usb_host_device_open(client, addr, &dev) != ESP_OK) {
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.println("device open failed");
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    return;
  }

  const usb_device_desc_t* dd;
  const usb_config_desc_t* cd;
  if (usb_host_get_device_descriptor(dev, &dd) == ESP_OK) {
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.printf("VID %04X PID %04X\n", dd->idVendor, dd->idProduct);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.printf("dev cls %02X/%02X/%02X\n",
        dd->bDeviceClass, dd->bDeviceSubClass, dd->bDeviceProtocol);
  }
  if (usb_host_get_active_config_descriptor(dev, &cd) == ESP_OK) {
    const uint8_t* p = (const uint8_t*)cd;
    const int total = cd->wTotalLength;
    for (int i = 0; i + 1 < total && p[i] > 0; i += p[i]) {
      if (p[i + 1] == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
        // class 07 = printer; the print firmware only claims these
        M5Cardputer.Display.printf("IF%d alt%d cls %02X/%02X/%02X\n",
            p[i + 2], p[i + 3], p[i + 5], p[i + 6], p[i + 7]);
      } else if (p[i + 1] == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
        const uint8_t ea = p[i + 2];
        const uint16_t mps = p[i + 4] | (p[i + 5] << 8);
        M5Cardputer.Display.printf(" EP %02X %s %s %u\n",
            ea, (ea & 0x80) ? "IN " : "OUT", epTypeName(p[i + 3]), mps);
      }
    }
  }
  usb_host_device_close(client, dev);
}

static void startHost() {
  M5Cardputer.Display.clear();
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.println("USB host started");
  M5Cardputer.Display.println("(power-cycle to reflash)");
  M5Cardputer.Display.println("Waiting for device...");
  M5Cardputer.Display.println("");

  const usb_host_config_t hostCfg = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };
  if (usb_host_install(&hostCfg) != ESP_OK) {
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.println("usb_host_install failed");
    return;
  }

  const usb_host_client_config_t clientCfg = {
    .is_synchronous = false,
    .max_num_event_msg = 8,
    .async = { .client_event_callback = clientCb, .callback_arg = nullptr },
  };
  if (usb_host_client_register(&clientCfg, &client) != ESP_OK) {
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.println("client_register failed");
    return;
  }
  started = true;
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setFont(&fonts::Font2);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.setTextScroll(true);

  M5Cardputer.Display.println("USB descriptor scan");
  M5Cardputer.Display.println("");
  M5Cardputer.Display.println("Connect the printer, turn it on,");
  M5Cardputer.Display.println("then press any key.");
  M5Cardputer.Display.println("");
  M5Cardputer.Display.println("(reflash works until you press)");
}

void loop() {
  M5Cardputer.update();

  if (!started) {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      startHost();
    }
    delay(10);
    return;
  }

  uint32_t flags;
  usb_host_lib_handle_events(0, &flags);
  usb_host_client_handle_events(client, 0);

  if (newDevAddr) {
    const uint8_t addr = newDevAddr;
    newDevAddr = 0;
    showDevice(addr);
  }
  if (devGone) {
    devGone = false;
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.println("device gone");
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  delay(1);
}
