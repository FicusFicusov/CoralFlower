// SPDX-License-Identifier: GPL-3.0-only
// Headless checks of CoralFlower's browser emulator (node >= 20, no browser needed for the functional part):
//   node tools/emu/web/test_web_emu.mjs [--no-native] [--no-page]        (sh tools/emu/web/build_web.sh first)
// 1. build/emu-web/coralflower.wasm instantiated with the worklet's stub imports: boots, draws, plays SCRIPT below
//    (Top + Lock, the default layout: M latched + C4, then M7 latched + C4) through the same exports the worklet uses,
//    checks its LED and sound expectations, the chords in the dump, the MIDI out, the settings saved to the flash by
//    themselves and read back by a second module booted from that image, the speed.
// 2. Determinism against the native headless runner (tools/emu/cf_headless.sh: the same emu_web.c built by cc): the
//    same script, its WAV; the samples must be identical (same input at the same millisecond, the firmware is
//    fixed-point), the dumps too.
// 3. The page: the site put together by tools/make_site.sh (the page is its /play/ and takes its look from
//    ../site.css), /play/?demo served by python3 -m http.server (else file://), its status line and a screenshot by
//    headless Chromium or Chrome (CHROME=path: another one).
import fs from "node:fs";
import path from "node:path";
import { execFileSync, spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "../../..");
const OUT = path.join(ROOT, "build/emu-web");
const TMP = process.env.TEST_TMP || path.join(ROOT, "build/emu-web-test");
const args = new Set(process.argv.slice(2));
fs.mkdirSync(TMP, { recursive: true });
await import(path.join(HERE, "keymap.js"));
const { BTN, BTN_ROLE, ENC, ENC_ROLE, noteName } = globalThis.FM1;

let fails = 0;
const ok = (c, msg) => { console.log(`  ${c ? "ok  " : "FAIL"}  ${msg}`); if (!c) fails++; return c; };

// The script, in tools/emu/cf_headless.c's words (cf_headless.sh runs it as it is, CF_BOOT_MS=0: from power-on);
// "expect" lines are this test's own (the native run gets them as comments). expect led NAME off|dim|on|mid|faint;
// expect sound|silence: the loudest sample of the last 100 ms recorded above / at most -50 dBFS (the effects' tail
// is still there, that quiet, 2 s after a release).
const SCRIPT = `
wav on
wait 1600
shot home
expect sound          # the boot sound, fading (silent from about 6.5 s)
key C#5 down          # M, the plain major button: it latches (Top + Lock)
key C#5 up
wait 100
expect led C#5 on
key C4 down           # the root: C major
wait 400
shot chord
expect led C4 on
expect sound
dump
turn K4 2             # GLIDE +2
wait 300
dump
key C4 up
wait 300
key G#3 down          # M7 latched in M's place
key G#3 up
wait 100
expect led G#3 on
expect led C#5 faint  # (an idle chord button)
key C4 down           # the same root: C major seventh
wait 400
shot maj7
expect sound
dump
key C4 up
wait 2000
expect silence
dump
`;

// ---------------------------------------------------------------- the module, as worklet.js loads it ---
let stdout = "";
async function load() {
  const bytes = fs.readFileSync(path.join(OUT, "coralflower.wasm"));
  let mem;
  const wasi = {
    clock_time_get: (id, prec, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
    fd_write: (fd, iov, n, out) => {   // the worklet drops the text; here it is kept (web_dump's lines)
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
    proc_exit: () => {},
  };
  const stub = new Proxy(wasi, { get: (t, k) => (k in t ? t[k] : () => 0) });
  const env = new Proxy({}, { get: () => () => 0 });
  const { instance } = await WebAssembly.instantiate(bytes, { wasi_snapshot_preview1: stub, env });
  const ex = instance.exports;
  mem = ex.memory;
  if (ex._initialize) ex._initialize();
  return ex;
}

// --------------------------------------------- tools/emu/cf_headless.c's script runner on the exports ---
const NOTE = {};
for (let k = 0; k < 27; k++) NOTE[noteName(k)] = k;
const ROLE = ["SELECT", "ALGORITHM", "PRESETS", "K1", "K2", "K3", "K4", "MASTER"];   // (cf_headless.c ROLE)
const LED = ["off", "dim", "on", "mid", "faint"];   // web_leds: 0 off 1 dim 2 lit 3 mid 4 faint
const ledState = (ex, name) => {
  const L = new Uint8Array(ex.memory.buffer, ex.web_leds(), 42);
  return LED[name === "GREEN" ? L[41] : name in NOTE ? L[NOTE[name]] : L[27 + BTN.indexOf(name)]];
};
const dbfs = (v) => (v ? `${(20 * Math.log10(v / 32768)).toFixed(1)} dBFS` : "0");
const fbCopy = (ex) => new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240).slice();
const fbLit = (fb) => fb.reduce((n, v) => n + (v !== 0), 0);
// pixels changed in the chord name's corner (top left, under the status bar): still while nothing changes (the plant
// animates elsewhere), about 70 for a name
const nameDiff = (a, b) => { let n = 0; for (let y = 16; y < 44; y++) for (let x = 0; x < 120; x++) n += a[240 * y + x] !== b[240 * y + x]; return n; };
// a sample as cf_headless writes it: (int16_t)(l * 32767) in float, clamped
const s16 = (l) => (l > 1 ? 32767 : l < -1 ? -32767 : Math.trunc(Math.fround(l * 32767)));

// step() as cf_headless.c's: without "wav on" the clock runs 10 ms at a time; with it, 1 ms at a time and 441 frames
// rendered every 10th (acc kept across calls, as its static); the MIDI out drained after every step
function runScript(ex, text, log) {
  let keys = 0, buttons = 0, wav = false, acc = 0;
  const pcm = [], midi = [], shots = {};
  const drain = () => { for (let p; (p = ex.web_midi_out_take()); ) midi.push(p >>> 0); };
  const step = (ms) => {
    if (!wav) {
      for (; ms > 0; ms -= 10) { ex.web_step(Math.min(10, ms)); drain(); }
      return;
    }
    while (ms-- > 0) {
      ex.web_step(1);
      drain();
      if (++acc >= 10) {
        acc = 0;
        ex.web_render(441);
        const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), 441), r = new Float32Array(ex.memory.buffer, ex.web_out_r(), 441);
        for (let j = 0; j < 441; j++) pcm.push(s16(l[j]), s16(r[j]));
      }
    }
  };
  const last100 = () => pcm.slice(-2 * 4410).reduce((m, v) => Math.max(m, Math.abs(v)), 0);
  for (const raw of text.split("\n")) {
    const line = raw.trim();
    if (!line || line.startsWith("#")) continue;
    const [c, a, b, d] = line.replace(/\s+#\s.*$/, "").split(/\s+/);
    if (c === "wait") step(+a);
    else if (c === "key" || c === "keyq") {
      const k = a in NOTE ? NOTE[a] : +a;
      if (!(k >= 0 && k < 27)) throw new Error(`script: unknown key ${a}`);
      keys = b === "up" ? keys & ~(1 << k) : keys | (1 << k);
      ex.web_keys(keys >>> 0);
      if (c === "key") step(20);
    } else if (c === "btn" || c === "hold") {
      const i = BTN.indexOf(a);
      if (i < 0) throw new Error(`script: unknown button ${a}`);
      if (c === "btn") { ex.web_buttons((buttons | (1 << i)) >>> 0); step(60); ex.web_buttons(buttons >>> 0); step(60); }
      else { buttons = b === "up" ? buttons & ~(1 << i) : buttons | (1 << i); ex.web_buttons(buttons >>> 0); step(20); }
    } else if (c === "turn") {
      if (ROLE.indexOf(a) < 0) throw new Error(`script: unknown knob ${a}`);
      ex.web_enc(ROLE.indexOf(a), parseInt(b, 10));
      step(40);
    } else if (c === "shot") { step(40); shots[a] = fbCopy(ex); }
    else if (c === "dump") { stdout = ""; ex.web_dump(); log.push(["dump", true, stdout]); }
    else if (c === "wav") wav = a === "on";
    else if (c === "expect" && a === "led") {
      const got = ledState(ex, b);
      log.push([`expect led ${b} ${d} at ${ex.web_ms()} ms`, got === d, got]);
    } else if (c === "expect" && (a === "sound" || a === "silence")) {
      const got = last100(), loud = got > 32768 * 10 ** (-50 / 20);
      log.push([`expect ${a} at ${ex.web_ms()} ms`, a === "sound" ? loud : !loud, `peak ${dbfs(got)} in the last 100 ms`]);
    } else throw new Error(`script: ${raw}`);
  }
  return { pcm: Int16Array.from(pcm), midi, shots };
}

const rms = (pcm) => Math.sqrt(pcm.reduce((s, v) => s + (v / 32768) ** 2, 0) / Math.max(1, pcm.length));
const dumpField = (text, re) => [...text.matchAll(re)].map((m) => m.slice(1));

// ===================================================================================================== 1 ===
console.log("browser emulator: build/emu-web/coralflower.wasm");
{   // the page's tables: web_buttons takes the labels in EMU_B_* order (tools/emu/emu_hooks.h), web_enc the EMU_E_* roles
  const h = fs.readFileSync(path.join(ROOT, "tools/emu/emu_hooks.h"), "utf8");
  const eb = /enum\s*{\s*(EMU_B_[^}]*)}/.exec(h)[1].split(",").map((s) => s.trim().replace(/^EMU_B_/, ""))
    .filter((s) => s && s !== "EMU_NB").map((s) => ({ OCTDN: "OCT-", OCTUP: "OCT+" })[s] || s);
  ok(BTN.join(" ") === eb.join(" ") && BTN_ROLE.length === BTN.length && ENC.length === 8 && ENC_ROLE.length === 8,
     `keymap.js: ${BTN.length} buttons in EMU_B_* order (${BTN.join(" ")}), a role for each; ${ENC.length} knobs`);
}
const ex = await load();
console.log(`  version: ${(() => { const m = new Uint8Array(ex.memory.buffer, ex.web_version()); return Buffer.from(m.subarray(0, m.indexOf(0))).toString(); })()}`);
const w0 = ex.web_lcd_writes();
ex.web_boot();
ok(ex.web_lcd_writes() > w0, "boot: the LCD was written");
const log = [];
const t0 = performance.now();
const run = runScript(ex, SCRIPT, log);
const secs = run.pcm.length / 2 / 44100, took = (performance.now() - t0) / 1000;
ok(fbLit(run.shots.home) > 1000, `after boot (1.6 s): ${fbLit(run.shots.home)} non-black pixels`);
const dChord = nameDiff(run.shots.home, run.shots.chord), dMaj7 = nameDiff(run.shots.chord, run.shots.maj7);
ok(dChord > 30, `M + C4: the chord's name on the screen (${dChord} pixels changed in its corner from the home screen)`);
ok(dMaj7 > 30, `M7 + C4: the new name (${dMaj7} pixels changed in the corner)`);
for (const [what, good, got] of log.filter((l) => l[0] !== "dump")) ok(good, `${what} (${got})`);
const dumps = log.filter((l) => l[0] === "dump").map((l) => l[2]);
const chordOf = (d) => /\n  chord: (\S+) sounding (\d) single \d buttons \w+ notes([\d ]*) {2}bass([\d ]*)/.exec(d) || [];
const voicesOf = (d, part) => +(new RegExp(`part ${part}: engine \\d+ voices (\\d+)`).exec(d) || [])[1];
const glideOf = (d) => +(/ glide (\d+)\n/.exec(d) || [])[1];
{
  const [, name, sounding, notes, bass] = chordOf(dumps[0]);
  ok(name === "C" && sounding === "1" && notes.trim() === "48 52 55" && bass.trim() === "36",
     `M + C4: ${name} sounding ${sounding}, notes ${notes.trim()}, bass ${bass.trim()} (web_dump)`);
  ok(voicesOf(dumps[0], 0) === 3 && voicesOf(dumps[0], 1) === 1, "the chord part plays the three notes, the bass part one");
  ok(glideOf(dumps[0]) === 0 && glideOf(dumps[1]) > 0, `K4 +2: GLIDE ${glideOf(dumps[0])} -> ${glideOf(dumps[1])} ms`);
}
{
  const [, name, sounding, notes] = chordOf(dumps[2]);
  ok(/^C(M7|maj7|Δ7?)$/.test(name) && sounding === "1" && notes.trim() === "48 52 55 59" && voicesOf(dumps[2], 0) === 4,
     `M7 + C4: ${name} sounding ${sounding}, notes ${notes.trim()}, ${voicesOf(dumps[2], 0)} voices`);
}
{
  const d = dumps[3];
  const gated = dumpField(d, /part (\d) gated:([\d ]*)\n/g).filter(([, n]) => n.trim());
  ok(chordOf(d)[2] === "0" && [0, 1, 2].every((p) => voicesOf(d, p) === 0) && !gated.length,
     "every part silent 2 s after the last release (no stuck note)");
}
const peakOf = (pcm) => pcm.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
const peak = peakOf(run.pcm), boot = peakOf(run.pcm.subarray(0, 2 * 1600 * 44.1));
ok(peak > 300, `audio: ${secs.toFixed(2)} s, peak ${(peak / 32768).toFixed(3)}, rms ${rms(run.pcm).toFixed(4)}`);
ok(boot > 0 && boot < peak, `the boot sound under the chords: peak ${dbfs(boot)} in the first 1.6 s, ${dbfs(peak)} in all`);
{   // USB-MIDI packets: byte 0 cable << 4 | CIN (9 note-on), then status, note, velocity
  const on = run.midi.filter((p) => (p & 0xf) === 9 && ((p >> 8) & 0xf0) === 0x90 && ((p >> 24) & 0xff) > 0);
  const ch = (c) => [...new Set(on.filter((p) => ((p >> 8) & 0xf) === c).map((p) => (p >> 16) & 0xff))].sort((a, b) => a - b);
  ok([48, 52, 55, 59].every((n) => ch(0).includes(n)) && ch(1).includes(36),
     `MIDI out: ${run.midi.length} packets, ${on.length} note-ons: the chords on channel 1 (${ch(0).join(" ")}), the bass on 2 (${ch(1).join(" ")})`);
}
ok(true, `speed: ${secs.toFixed(2)} s of device time in ${took.toFixed(2)} s (${(secs / took).toFixed(1)}x real time)`);

// the flash: a setting changed (HOME's K2, the key) is written by itself a while later, once nothing sounds
const settingsOf = (e) => { stdout = ""; e.web_dump(); return /settings: (\w+), key (\d+), colour \d+, saved (\w+) now (\w+)/.exec(stdout) || []; };
{
  const d0 = ex.web_flash_dirty();
  const k0 = +settingsOf(ex)[2];
  ex.web_enc(ROLE.indexOf("K2"), 2);
  const run10 = () => { ex.web_step(10); while (ex.web_avail()) ex.web_render(Math.min(1024, ex.web_avail())); };
  let ms = 0;
  for (; ms < 10000 && ex.web_flash_dirty() === d0; ms += 10) run10();
  for (let s = 0; s < 1000; s += 10) run10();   // (the erase and the program done)
  const [, , key, saved, now] = settingsOf(ex);
  ok(ex.web_flash_dirty() > d0 && +key === k0 + 2 && saved === now,
     `flash: K2 +2 (key ${k0} -> ${key}) saved by itself after ${(ms / 1000).toFixed(2)} s (dirty counter ${d0} -> ` +
     `${ex.web_flash_dirty()}, settings check ${saved}); web_flash_size ${ex.web_flash_size()}`);
  // and it comes back: a second module booted from the saved image reads the record (the IndexedDB round trip)
  const img = new Uint8Array(ex.memory.buffer, ex.web_flash(), ex.web_flash_size()).slice();
  const ex2 = await load();
  new Uint8Array(ex2.memory.buffer, ex2.web_flash_stage(), img.length).set(img);
  ex2.web_flash_stage_len(img.length);
  ex2.web_boot();
  ex2.web_step(300);
  const [, from2, key2, , now2] = settingsOf(ex2);
  ok(from2 === "loaded" && key2 === key && now2 === now, `flash round trip: settings ${from2}, key ${key2} after a reboot from the saved image`);
}

// the worklet's view: 128-frame quanta while a chord plays (a UI frame lands in some of them)
{
  ex.web_keys(1 << NOTE.C4);   // (M7 still latched)
  const ts = [];
  for (let q = 0; q < 3 * 44100 / 128; q++) { const t = performance.now(); ex.web_render(128); ts.push(performance.now() - t); }
  ex.web_keys(0);
  ts.sort((a, b) => a - b);
  const avg = ts.reduce((a, b) => a + b, 0) / ts.length, budget = 128 / 44.1;
  ok(ts[ts.length - 1] < budget * 4, `render quantum (128 frames, budget ${budget.toFixed(2)} ms): avg ${avg.toFixed(3)} ms, ` +
     `99th ${ts[Math.floor(ts.length * 0.99)].toFixed(3)} ms, max ${ts[ts.length - 1].toFixed(3)} ms (node, this machine)`);
}

// ===================================================================================================== 2 ===
if (!args.has("--no-native")) {
  console.log("determinism against the native headless runner (tools/emu/cf_headless.sh)");
  const dir = path.join(TMP, "native"), scr = path.join(TMP, "web_test_native.txt");
  fs.rmSync(dir, { recursive: true, force: true });
  fs.writeFileSync(scr, SCRIPT.split("\n").map((l) => (/^\s*expect\b/.test(l) ? "# " + l : l)).join("\n"));
  let text = "";
  try {
    text = execFileSync("sh", [path.join(ROOT, "tools/emu/cf_headless.sh"), scr, dir], { cwd: ROOT, env: { ...process.env, CF_BOOT_MS: "0" }, encoding: "latin1", maxBuffer: 64 << 20 });
  } catch (e) { console.log(`  skip  bit-exact comparison: cf_headless.sh failed (${String(e.stderr || e.message).trim().split("\n").pop()})`); }
  const wav = path.join(dir, "out.wav");
  if (fs.existsSync(wav)) {
    const b = fs.readFileSync(wav);
    const nat = new Int16Array(b.buffer.slice(b.byteOffset + 44, b.byteOffset + b.length));
    let first = -1;
    const n = Math.min(nat.length, run.pcm.length);
    for (let i = 0; i < n; i++) if (nat[i] !== run.pcm[i]) { first = i; break; }
    if (nat.length === run.pcm.length && first < 0) ok(true, `bit-exact: ${nat.length / 2} stereo frames identical to the native runner's WAV`);
    else {
      ok(false, `samples differ: native ${nat.length / 2} frames, web ${run.pcm.length / 2}; first difference at frame ${first >> 1}`);
      ok(Math.abs(rms(nat) - rms(run.pcm)) < 0.25 * rms(nat), `loosely: rms native ${rms(nat).toFixed(4)}, web ${rms(run.pcm).toFixed(4)}`);
    }
    // the dumps: the native one adds its MIDI count, and the boot line comes before the first; the screen tiles sent
    // are left out (cf_headless.sh builds "CoralFlower dev", and the splash draws the version: a tile or two more)
    const tiles = (d) => d.replace(/tiles sent \d+/, "tiles sent -");
    const nd = text.split(/^(?=fm1_in: )/m).slice(1).map((d) => tiles(d.replace(/^  midi out: .*\n/m, "")));
    const same = nd.length === dumps.length && nd.every((d, i) => d === tiles(dumps[i]));
    ok(same, `the ${dumps.length} dumps identical to the native runner's` + (same ? "" : ` (native ${nd.length})`));
  }
}

// ===================================================================================================== 3 ===
if (!args.has("--no-page")) {
  console.log("the page (headless Chromium: /play/?demo of the site)");
  const chrome = [process.env.CHROME, "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"].find((p) => p && fs.existsSync(p));
  if (!chrome) console.log("  skip  the page: no Chromium or Chrome (CHROME=path)");
  else {
    const site = path.join(TMP, "site");
    fs.rmSync(site, { recursive: true, force: true });
    execFileSync("sh", [path.join(ROOT, "tools/make_site.sh"), site], { cwd: ROOT, stdio: "pipe" });
    const port = 18000 + Math.floor(Math.random() * 2000);
    const srv = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1"], { cwd: site, stdio: "ignore" });
    await new Promise((r) => setTimeout(r, 800));
    const flags = ["--headless=new", "--use-mock-keychain", "--password-store=basic", "--disable-gpu", "--no-sandbox", "--no-first-run",
      "--no-default-browser-check", `--user-data-dir=${path.join(TMP, "chrome-profile-emu")}`, "--hide-scrollbars",
      "--allow-file-access-from-files", "--window-size=1280,1400", "--timeout=20000", "--virtual-time-budget=4000"];
    const kill = () => { try { execFileSync("pkill", ["-f", "chrome-profile-emu"]); } catch {} };
    const statusOf = (url) => {   // the page's status line once its scripts ran (emu.js: "Ready. Press Start for sound.")
      try {
        const dom = execFileSync(chrome, [...flags, "--dump-dom", url], { encoding: "utf8", timeout: 30000, stdio: ["ignore", "pipe", "ignore"] });
        return (/id="status"[^>]*>([^<]*)</.exec(dom) || [, "(no status line)"])[1];
      } catch (e) { return "(" + e.message.split("\n")[0] + ")"; } finally { kill(); }
    };
    const shoot = async (url, png) => {
      try { fs.unlinkSync(png); } catch {}
      const c = spawn(chrome, [...flags, `--screenshot=${png}`, url], { stdio: "ignore" });
      for (let i = 0; i < 60 && !(fs.existsSync(png) && fs.statSync(png).size > 0); i++) await new Promise((r) => setTimeout(r, 500));
      await new Promise((r) => setTimeout(r, 500));
      c.kill();
      kill();
      return fs.existsSync(png) && fs.statSync(png).size > 20000;   // (a blank error page is small)
    };
    const png = path.join(TMP, "page.png");
    let url = `http://127.0.0.1:${port}/play/index.html?demo`;
    let st = statusOf(url);
    if (!/^Ready\b/.test(st)) { url = `file://${path.join(site, "play/index.html")}?demo`; st = statusOf(url); }
    ok(/^Ready\b/.test(st), `the wasm loaded and booted in the page: "${st}" (${url.split(":")[0]})`);
    ok(await shoot(url, png), `screenshot: ${png}`);
    srv.kill();
  }
}

console.log(fails ? `FAIL (${fails})` : "PASS");
process.exit(fails ? 1 : 0);
