import { spawnSync } from 'node:child_process';
import { detectPort } from './port.mjs';
import { FQBN } from './fqbn.mjs';

const SKETCH = 'sketch';

function fail(msg) {
  console.error(`flash: ${msg}`);
  process.exit(1);
}

let port = process.argv[2];
if (!port) {
  try {
    const found = detectPort();
    port = found.address;
    console.log(`flash: using ${port} (${found.chip})`);
  } catch (e) {
    fail(`${e.message}\n  Or: npm run flash -- COM10`);
  }
}

// -p must precede the sketch name; arduino-cli accepts only one positional arg.
const r = spawnSync(
  'arduino-cli',
  ['compile', '-u', '-p', port, '--fqbn', FQBN, SKETCH],
  { stdio: 'inherit', shell: true },
);
process.exit(r.status ?? 1);
