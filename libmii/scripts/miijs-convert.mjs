// Converts every .charinfo in a directory to .nfcd / .nfsd / .ffsd with MiiJS,
// an independent implementation, to produce reference test vectors.
//
//   node miijs-convert.mjs <path-to-MiiJS-checkout> <vector-dir>
import fs from 'fs';
import path from 'path';
import { pathToFileURL } from 'url';

const [lib, dir] = process.argv.slice(2);
if (!lib || !dir) {
  console.error('usage: node miijs-convert.mjs <MiiJS dir> <vector dir>');
  process.exit(2);
}
const { decodeMii, encodeMii } = await import(pathToFileURL(path.join(lib, 'miiProcess.js')));
const { MiiFormats } = await import(pathToFileURL(path.join(lib, 'formats.js')));

let n = 0;
for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.charinfo')).sort()) {
  const base = path.join(dir, f.slice(0, -'.charinfo'.length));
  const mii = decodeMii(fs.readFileSync(path.join(dir, f)));
  fs.writeFileSync(base + '.nfcd', Buffer.from(encodeMii(mii, MiiFormats.NFCD)));
  fs.writeFileSync(base + '.nfsd', Buffer.from(encodeMii(mii, MiiFormats.NFSD)));
  fs.writeFileSync(base + '.ffsd', Buffer.from(encodeMii(mii, MiiFormats.FFSD)));
  n++;
}
console.log(`wrote vectors for ${n} Miis`);
