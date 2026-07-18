import { spawnSync } from 'node:child_process';

// No USB-UART bridge on the Cardputer-Adv; the ESP32-S3 itself enumerates as
// Espressif USB Serial/JTAG. vid must be compared lowercase.
const USB_SERIAL_CHIPS = [
  { vid: '0x303a', name: 'ESP32-S3 USB Serial/JTAG' },
];

export function detectPort() {
  const r = spawnSync('arduino-cli', ['board', 'list', '--json'], { encoding: 'utf8', shell: true });
  if (r.status !== 0) throw new Error(`arduino-cli board list failed\n${r.stderr ?? ''}`);

  let detected;
  try {
    detected = JSON.parse(r.stdout).detected_ports ?? [];
  } catch {
    throw new Error('could not parse board list JSON');
  }

  const candidates = [];
  for (const d of detected) {
    const p = d.port;
    if (!p || p.protocol !== 'serial') continue;
    const vid = p.properties?.vid?.toLowerCase();
    const chip = USB_SERIAL_CHIPS.find((c) => c.vid === vid);
    if (chip) candidates.push({ address: p.address, chip: chip.name });
  }

  if (candidates.length === 0) {
    throw new Error('Cardputer not found. Check the USB connection.\n' +
      '  If you have already printed once, the USB port is in host mode:\n' +
      '  power-cycle the Cardputer (power switch) and reconnect.\n' +
      '  If you know the port, pass it as an argument, e.g. COM10');
  }
  if (candidates.length > 1) {
    const list = candidates.map((c) => `${c.address} (${c.chip})`).join(', ');
    throw new Error(`multiple USB serial ports found: ${list}\n  Pass the one you want as an argument.`);
  }
  return candidates[0];
}
