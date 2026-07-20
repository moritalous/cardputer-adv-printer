#include <M5Cardputer.h>
#include "usb_printer.h"
#include "content.h"

const int WIDTH = 464;
const int BPL   = WIDTH / 8;

const int MARGIN_L   = 40;
const int CONTENT_X  = MARGIN_L;
// Must match CONTENT_W in tools/gen-content.mjs.
const int CONTENT_W  = WIDTH - MARGIN_L * 2;
const int LINE_H     = 28;
const int IMAGE_GAP  = 12;
const int BOTTOM_PAD = 4;
// 1bpp: no PSRAM on the Cardputer-Adv, 16bpp would need 1.8MB.
const int MAX_CANVAS_H = 2000;

const int LABEL_H = 18;

// Single GS v 0 block for the whole job. Splitting makes the printer feed
// paper between blocks, leaving white seams. Cold-start job loss is fixed by
// the handshake in usb_printer.cpp, not by splitting.
const int MAX_BLOCK_ROWS = MAX_CANVAS_H;

const size_t MAX_TEXT = 1000;

const char* INPUT_PROMPT = "Enter your name";

const uint8_t SPK_VOLUME = 128;  // 0-255

// Stamp-S3A onboard WS2812. GPIO38 gates its power rail and must be driven
// high before the data pin does anything (Cardputer ADV docs).
const int LED_PWR_PIN  = 38;
const int LED_DATA_PIN = 21;

M5Canvas canvas(&M5Cardputer.Display);

String text;
bool busy = false;

void ledColor(uint8_t r, uint8_t g, uint8_t b) {
  neopixelWrite(LED_DATA_PIN, r, g, b);
}

void ledOff() {
  ledColor(0, 0, 0);
}

// tone() is fire-and-forget; sequences interleave delay() so each note is heard.
void soundKey()   { M5Cardputer.Speaker.tone(6000, 15); }
void soundDel()   { M5Cardputer.Speaker.tone(3000, 20); }

void soundClear() {
  M5Cardputer.Speaker.tone(2000, 60);
  delay(70);
  M5Cardputer.Speaker.tone(1200, 90);
}

// Typewriter bell on Enter.
void soundBell()  { M5Cardputer.Speaker.tone(2794, 120); }

void soundOk() {
  const uint16_t seq[][2] = { { 1047, 90 }, { 1319, 90 }, { 1568, 140 } };
  for (auto& n : seq) {
    M5Cardputer.Speaker.tone(n[0], n[1]);
    delay(n[1] + 15);
  }
}

void soundError() {
  for (int i = 0; i < 2; i++) {
    M5Cardputer.Speaker.tone(220, 120);
    delay(160);
  }
}

String expandTokens(const char* t) {
  String s(t);
  s.replace("{{input}}", text);
  if (content_verb_count > 0 && s.indexOf("{{verb}}") >= 0) {
    s.replace("{{verb}}", content_verbs[esp_random() % content_verb_count]);
  }
  return s;
}

int drawContent() {
  canvas.fillSprite(TFT_WHITE);
  canvas.setTextColor(TFT_BLACK);
  canvas.setFont(&fonts::lgfxJapanGothic_20);

  // Do not add setClipRect here: it moves the wrap boundary from 464 to 424
  // and reflows lines that currently fit.

  // Measures y_advance; LovyanGFX exposes no line-height getter.
  canvas.setCursor(CONTENT_X, 0);
  canvas.println("");
  int yAdvance = canvas.getCursorY();
  if (yAdvance <= 0) yAdvance = LINE_H;

  canvas.setCursor(CONTENT_X, 0);
  for (size_t i = 0; i < content_block_count; i++) {
    const content_block_t& b = content_blocks[i];

    if (b.type == CONTENT_BLOCK_IMAGE) {
      int y = canvas.getCursorY();
      int x = CONTENT_X + (CONTENT_W - (int)b.image_w) / 2;
      // Packed 1bpp, bit 1 = black. Not drawPng: the PNG decoder mangles
      // colors on palette sprites (everything comes out black).
      canvas.drawBitmap(x, y, b.image_data, b.image_w, b.image_h, TFT_BLACK, TFT_WHITE);
      canvas.setCursor(CONTENT_X, y + b.image_h + IMAGE_GAP);
    } else {
      int startY = canvas.getCursorY();
      canvas.setCursor(CONTENT_X, startY);
      canvas.print(expandTokens(b.text));
      int rows = (canvas.getCursorY() - startY) / yAdvance + 1;
      canvas.setCursor(CONTENT_X, startY + rows * LINE_H);
    }
  }
  return canvas.getCursorY() + BOTTOM_PAD;
}

void drawLabels() {
  auto& d = M5Cardputer.Display;
  const int y = d.height() - LABEL_H;
  d.fillRect(0, y, d.width(), LABEL_H, TFT_BLACK);
  d.drawFastHLine(0, y, d.width(), TFT_DARKGREY);

  d.setFont(&fonts::lgfxJapanGothic_12);
  d.setTextDatum(middle_left);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.drawString("Enter:印刷", 4, y + LABEL_H / 2);
  d.drawString("esc:全消去", 100, y + LABEL_H / 2);
  d.setTextDatum(top_left);
}

void drawEditor() {
  auto& d = M5Cardputer.Display;
  const int viewH = d.height() - LABEL_H;

  d.setClipRect(0, 0, d.width(), viewH);
  d.fillRect(0, 0, d.width(), viewH, TFT_BLACK);

  d.setFont(&fonts::lgfxJapanGothic_16);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setTextDatum(top_center);
  d.drawString(INPUT_PROMPT, d.width() / 2, 4);

  // Input line, vertically centered, left-aligned. ASCII-only input, so an
  // ASCII font costs almost no flash (unlike lgfxJapanGothic_*).
  d.setFont(&fonts::FreeSansBold18pt7b);
  const int cy = viewH / 2 + 8;
  const int wText = text.length() ? d.textWidth(text) : 0;
  const int wCur = d.textWidth("_");

  int x = 4;
  if (wText + wCur > d.width() - 8) {
    x = d.width() - 4 - wCur - wText;      // overflow: keep the tail visible
  }
  d.setTextDatum(middle_left);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  if (text.length()) d.drawString(text, x, cy);
  d.setTextColor(TFT_GREEN, TFT_BLACK);
  d.drawString("_", x + wText, cy);

  d.setTextDatum(top_left);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.clearClipRect();

  drawLabels();
}

bool sendPrintJob(int h) {
  uint8_t header[] = {
    0x1B, 0x4E, 0x0D, 0x05,
    0x1B, 0x4E, 0x04, 0x0A,
    0x1F, 0x11, 0x0B
  };
  if (!usbPrinterWrite(header, sizeof(header))) return false;

  // The 1bpp framebuffer is already printer-format rows (58 bytes, MSB
  // leftmost); only the sense differs (palette 1 = white, printer 1 = black),
  // so each row is an inverted copy.
  const uint8_t* fb = (const uint8_t*)canvas.getBuffer();
  static uint8_t buf[BPL * 64];

  for (int blockY = 0; blockY < h; blockY += MAX_BLOCK_ROWS) {
    const int rows = min(MAX_BLOCK_ROWS, h - blockY);

    uint8_t blk[] = {
      0x1D, 0x76, 0x30, 0x00,
      (uint8_t)(BPL & 0xFF), (uint8_t)(BPL >> 8),
      (uint8_t)(rows & 0xFF), (uint8_t)(rows >> 8)
    };
    if (!usbPrinterWrite(blk, sizeof(blk))) return false;

    int used = 0;
    for (int i = 0; i < rows; i++) {
      const uint8_t* src = fb + (size_t)(blockY + i) * BPL;
      for (int b = 0; b < BPL; b++) {
        buf[used + b] = ~src[b];
      }
      used += BPL;
      if (used + BPL > (int)sizeof(buf)) {
        if (!usbPrinterWrite(buf, used)) return false;
        used = 0;
      }
    }
    if (used > 0 && !usbPrinterWrite(buf, used)) return false;
  }

  uint8_t footer[] = { 0x1F, 0xF0, 0x05, 0x00, 0x1F, 0xF0, 0x03, 0x00 };
  return usbPrinterWrite(footer, sizeof(footer));
}

void statusScreen() {
  auto& d = M5Cardputer.Display;
  d.clearClipRect();
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::lgfxJapanGothic_16);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setTextScroll(true);
  d.setCursor(0, 0);
}

void doPrint() {
  busy = true;
  ledColor(0, 40, 60);  // cyan: printing
  soundBell();
  statusScreen();
  auto& d = M5Cardputer.Display;

  if (!usbPrinterStarted()) {
    d.println("Starting USB host...");
    d.println("(reflash needs a power");
    d.println(" cycle from now on)");
    if (!usbPrinterStart()) {
      d.setTextColor(TFT_RED, TFT_BLACK);
      d.println(usbPrinterStatus());
      ledColor(60, 0, 0);
      soundError();
      delay(2500);
      ledOff();
      busy = false;
      drawEditor();
      return;
    }
  }

  d.println("Waiting for printer...");
  if (!usbPrinterWaitReady(5000)) {
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.println(usbPrinterStatus());
    d.setTextColor(TFT_WHITE, TFT_BLACK);
    d.println("Check cable & power.");
    ledColor(60, 0, 0);
    soundError();
    delay(2500);
    ledOff();
    busy = false;
    drawEditor();
    return;
  }

  // Rendered at print time so {{input}} picks up the current text.
  const int h = drawContent();
  if (h <= 0 || h > MAX_CANVAS_H) {
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.println(h > MAX_CANVAS_H ? "Content too tall" : "Render failed");
    ledColor(60, 0, 0);
    soundError();
    delay(2500);
    ledOff();
    busy = false;
    drawEditor();
    return;
  }

  d.println("Printing...");
  const bool printed = sendPrintJob(h);

  if (printed) {
    d.setTextColor(TFT_GREEN, TFT_BLACK);
    d.println("Done");
    ledColor(0, 60, 0);
    soundOk();
    text = "";  // clear the input on success, same as esc
  } else {
    d.setTextColor(TFT_RED, TFT_BLACK);
    d.println(usbPrinterStatus());
    ledColor(60, 0, 0);
    soundError();
  }

  // The USB link stays connected between jobs; no disconnect, no drain wait.
  delay(1000);
  ledOff();
  busy = false;
  drawEditor();
}

void showFatal(const char* msg) {
  auto& d = M5Cardputer.Display;
  d.clearClipRect();
  d.fillScreen(TFT_BLACK);
  d.setFont(&fonts::lgfxJapanGothic_16);
  d.setTextColor(TFT_RED, TFT_BLACK);
  d.setCursor(0, 0);
  d.println(msg);
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Speaker.setVolume(SPK_VOLUME);

  pinMode(LED_PWR_PIN, OUTPUT);
  digitalWrite(LED_PWR_PIN, HIGH);
  ledOff();

  canvas.setColorDepth(1);
  if (!canvas.createSprite(WIDTH, MAX_CANVAS_H)) {
    showFatal("Sprite alloc failed");
    return;
  }

  // Sanity-render the template once; input can only add wrapped lines.
  const int h = drawContent();
  if (h <= 0) {
    showFatal("Render failed");
    return;
  }
  if (h > MAX_CANVAS_H) {
    showFatal("Content too tall");
    M5Cardputer.Display.printf("%d > %d\n", h, MAX_CANVAS_H);
    return;
  }

  drawEditor();
}

void loop() {
  M5Cardputer.update();
  usbPrinterPump();

  if (!busy && M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto st = M5Cardputer.Keyboard.keysState();

    if (st.enter) {
      doPrint();
    } else {
      bool changed = false;
      if (st.del && text.length() > 0) {
        text.remove(text.length() - 1);
        soundDel();
        changed = true;
      }
      for (char c : st.word) {
        // Top-left key (labeled esc) clears everything.
        if (c == '`' || c == '~') {
          if (text.length() > 0) {
            text = "";
            soundClear();
            changed = true;
          }
        } else if (text.length() < MAX_TEXT) {
          text += c;
          soundKey();
          changed = true;
        }
      }
      if (changed) drawEditor();
    }
  }

  delay(10);
}
