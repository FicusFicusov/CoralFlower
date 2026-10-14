// SPDX-License-Identifier: GPL-3.0-only
// CoralFlower: run the browser emulator's wasm headless in node, play a small script, save screenshots.
//   node tools/emu/web/cf_run.mjs SCRIPT [OUTDIR]
// SCRIPT lines: "wait MS", "key K down|up" (K: 0..26 or a note name like C4), "btn NAME" (a tap: FX SEL ENV LFO
// EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+), "hold NAME down|up", "turn ROLE N" (SELECT ALGORITHM PRESETS
// K1..K4), "shot NAME" (OUTDIR/NAME.ppm), "dump" (the firmware's state to stdout).
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "../../..");
await import(path.join(HERE, "keymap.js"));
const { BTN, noteName } = globalThis.FM1;
const script = fs.readFileSync(process.argv[2], "utf8");
const outDir = process.argv[3] || path.join(ROOT, "build/cf-run");
fs.mkdirSync(outDir, { recursive: true });

let stdout = "", mem;
const wasi = {
  clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => {
    const dv = new DataView(mem.buffer);
    let len = 0;
    for (let i = 0; i < n; i++) {
      const p = dv.getUint32(iov + 8 * i, true), l = dv.getUint32(iov + 8 * i + 4, true);
      stdout += Buffer.from(new Uint8Array(mem.buffer, p, l)).toString("latin1");
      len += l;
    }
    dv.setUint32(out, len, true);
    return 0;
  },
  proc_exit: (c) => { throw new Error("exit " + c); },
};
const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
const env = new Proxy({}, { get: () => () => 0 });
const bytes = fs.readFileSync(path.join(ROOT, "build/emu-web/coralflower.wasm"));
const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: stub, env });
const ex = instance.exports;
mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_boot();

const flush = () => { if (stdout) { process.stdout.write(stdout); stdout = ""; } };
const NOTE = {};
for (let k = 0; k < 27; k++) NOTE[noteName(k)] = k;
const ROLE = { SELECT: 0, ALGORITHM: 1, PRESETS: 2, K1: 3, K2: 4, K3: 5, K4: 6, MASTER: 7 };
let keys = 0, buttons = 0;
const step = (ms) => { ex.web_step(ms); flush(); };
const shot = (name) => {
  const fb = new Uint16Array(mem.buffer, ex.web_fb(), 240 * 240);
  const rgb = Buffer.alloc(240 * 240 * 3);
  for (let i = 0; i < 240 * 240; i++) {
    const u = ((fb[i] >> 8) | (fb[i] << 8)) & 0xffff;
    rgb[3 * i] = ((u >> 11) & 31) * 255 / 31; rgb[3 * i + 1] = ((u >> 5) & 63) * 255 / 63; rgb[3 * i + 2] = (u & 31) * 255 / 31;
  }
  fs.writeFileSync(path.join(outDir, name + ".ppm"), Buffer.concat([Buffer.from("P6\n240 240\n255\n"), rgb]));
};
step(1500);
for (const raw of script.split("\n")) {
  const line = raw.replace(/#.*/, "").trim();
  if (!line) continue;
  const [cmd, a, b] = line.split(/\s+/);
  if (cmd === "wait") step(+a);
  else if (cmd === "key") {
    const k = a in NOTE ? NOTE[a] : +a;
    keys = b === "up" ? keys & ~(1 << k) : keys | (1 << k);
    ex.web_keys(keys >>> 0);
    step(20);
  } else if (cmd === "btn" || cmd === "hold") {
    const i = BTN.indexOf(a);
    if (cmd === "btn") { ex.web_buttons((buttons | (1 << i)) >>> 0); step(60); ex.web_buttons(buttons >>> 0); step(60); }
    else { buttons = b === "up" ? buttons & ~(1 << i) : buttons | (1 << i); ex.web_buttons(buttons >>> 0); step(20); }
  } else if (cmd === "turn") { ex.web_enc(ROLE[a], +b); step(40); }
  else if (cmd === "shot") { step(40); shot(a); }
  else if (cmd === "dump") { ex.web_dump(); flush(); }
  else console.log("? " + line);
}
flush();
