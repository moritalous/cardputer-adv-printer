import { spawnSync } from 'node:child_process';
import { detectPort } from './port.mjs';
import { FQBN } from './fqbn.mjs';

const SKETCH = 'scan';

// Results cannot be harvested over serial: the serial port dies the moment
// the scan firmware starts the USB host (shared PHY), so the firmware shows
// everything on the Cardputer's own display.

function fail(msg) {
  console.error(`scan: ${msg}`);
  process.exit(1);
}

let port = process.argv[2];
if (!port) {
  try {
    const found = detectPort();
    port = found.address;
    console.log(`scan: using ${port} (${found.chip})`);
  } catch (e) {
    fail(`${e.message}\n  Or: npm run scan -- COM10`);
  }
}

console.log('scan: flashing the USB descriptor viewer (this replaces the print firmware)...');
const up = spawnSync(
  'arduino-cli',
  ['compile', '-u', '-p', port, '--fqbn', FQBN, SKETCH],
  { stdio: ['inherit', 'ignore', 'inherit'], shell: true },
);
if (up.status !== 0) fail('could not flash the scan firmware');

console.log(`
scan: done. Results appear on the Cardputer's display, not here:

  1. Unplug the Cardputer from the PC.
  2. Attach the USB-A adapter and connect the printer. Turn the printer on.
  3. Press any key on the Cardputer to start the USB host.
  4. Read the display. A printer shows an interface line "cls 07/01/xx"
     and an "EP xx OUT bulk" endpoint.

The print firmware only talks to interfaces of class 07 (USB printer class).
When finished: power-cycle the Cardputer, reconnect it to the PC,
then restore the print firmware with: npm run flash
`);
