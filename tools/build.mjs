import { spawnSync } from 'node:child_process';
import { FQBN } from './fqbn.mjs';

const r = spawnSync(
  'arduino-cli',
  ['compile', '--fqbn', FQBN, 'sketch'],
  { stdio: 'inherit', shell: true },
);
process.exit(r.status ?? 1);
