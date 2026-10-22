// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
// Copyright (C) 2026 CoralFlower contributors
//
// Node checks of CoralFlower's installer, on the site's first page web/index.html (no browser, no hardware), after
// ChoralRoot's web/test_installer.mjs. Run from the repo root:   node web/test_installer.mjs
// - the classification (fm1ota.js classifyFirmware): ChoralRoot's table unchanged, CoralFlower over CoralFlower allowed;
//   tools/fm1_install.py classify_firmware gives the same answers (when python3 is there); fm1backup.js backupFamily
// - the page's shape: one module script that imports fm1pkg.js, fm1ota.js and fm1backup.js from its own folder (every
//   name it imports is exported there), nothing from elsewhere; the look from site.css alone (no <style>, no colour in
//   the markup), shared with the emulator page tools/emu/web/index.html, which has the same header, Support button,
//   card, dialog and script; web/install/index.html forwards to /#install; no ChoralRoot-only parts
// - the texts: every key the script names, and every error code fm1ota.js throws, has an English text
// - the page itself (its module script with the three libraries inlined, as a browser links them) against a DOM stub and
//   a simulated FM-1: no Web MIDI; no releases.json (a file only); the file check (a well-formed FM-1_930 package made
//   by a JS port of tools/fm1pkg_make.py accepted; another identity, the official firmware, no Felucca loader, a damaged
//   file, not a package: refused, Install stays off); installs over the stock firmware, CoralFlower, ChoralRoot,
//   Felucca (backup, then the restore offered), the refusals and the override, resume from update mode, the errors;
//   "What is on my FM-1?"; the hosted versions of releases.json (sha256-checked); Back up and Restore; the return to V15

import { existsSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { tmpdir } from "node:os";
import { spawnSync } from "node:child_process";
import vm from "node:vm";
import { Package, logicalImage, productOf, crc16, STOCK_V15_SIZE } from "./fm1pkg.js";
import { pack7, unpack7, classifyFirmware, refusalText, CF_EARLY } from "./fm1ota.js";
import { bkU32, bkR32, bkPack, bkUnpack, bkCrc, CR_BACKUP_IDS, CF_BACKUP_IDS, CF_EDIT_IDS, BACKUP_IDS, backupFamily, backupFileName, objectNames, readBackup } from "./fm1backup.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(100)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const HERE = new URL(".", import.meta.url).pathname;
const ROOT = join(HERE, "..");
const count = (s, sub) => s.split(sub).length - 1;
const html = readFileSync(join(HERE, "index.html"), "utf8");
const css = readFileSync(join(HERE, "site.css"), "utf8");
const fwd = readFileSync(join(HERE, "install/index.html"), "utf8");
const emu = readFileSync(join(ROOT, "tools/emu/web/index.html"), "utf8");
const ota = readFileSync(join(HERE, "fm1ota.js"), "utf8");
const sha256 = async (b) => Buffer.from(await crypto.subtle.digest("SHA-256", b)).toString("hex");
const realSleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(cond, ms = 4000) {         // real time: the page's own waits run at a tenth (fastTimeout)
  const end = Date.now() + ms;
  while (Date.now() < end) { if (cond()) return true; await realSleep(5); }
  return cond();
}

/* ------------------------------------------------------ (a) the classification --- */
const GUARD_CASES = [                            // ChoralRoot's tests/install_test.py table, and CoralFlower's rows
  ["FM-1_015", null, "allow", "stock"], ["FM-1_014", null, "allow", "stock"],
  ["FM-1_920", "ChoralRoot 0.12", "allow", "choralroot"], ["FM-1_920", "ChoralRoot 1.0-rc1", "allow", "choralroot"],
  ["FM-1_910", "FELUCCA v1.0", "allow", "felucca"], ["FM-1_910", "FELUCCA v1.0.1", "allow", "felucca"],
  ["FM-1_910", "FELUCCA 1.0", "allow", "felucca"], ["FM-1_911", "FELUCCA v1.1-rc1", "allow", "felucca"],
  ["FM-1_90111", "MELODEE v0.11.1", "allow", "melodee"], ["FM-1_910", "MELODEE v1.0", "allow", "melodee"],
  ["FM-1_909", "FELUCCA 0.9-BETA", "refuse", "felucca-beta"], ["FM-1_905", "FELUCCA 0.5 BETA", "refuse", "felucca-beta"],
  ["FM-1_900", "FELUCCA SLOOP 2.2", "refuse", "sloop"], ["FM-1_922", "FELUCCA 2.2 BETA", "refuse", "felucca-beta"],
  ["FM-1_920", "FELUCCA 2.0 BETA", "refuse", "felucca-beta"], ["FM-1_000", null, "refuse", "sloop"],
  ["FM-1_900", null, "refuse", "unknown"], ["FM-1_920", null, "refuse", "unknown"], ["FM-1_500", null, "refuse", "unknown"],
  ["FM-1_910", "SOMETHING 3.0", "refuse", "unknown"], ["ota-FM-1_920", null, "loader", "loader"],
  ["FM-1_930", "CoralFlower 0.1", "allow", "coralflower"], ["FM-1_930", "CoralFlower 1.0-rc1", "allow", "coralflower"],
  ["FM-1_930", "CORALFLOWER 0.2", "allow", "coralflower"], ["FM-1_930", "CoralFlowerX 1", "refuse", "unknown"],
  ["FM-1_930", null, "allow", "coralflower"], ["FM-1_930", "", "allow", "coralflower"],   // an early CoralFlower: no INFO
  ["FM-1_931", null, "refuse", "unknown"], ["FM-1_9300", null, "refuse", "unknown"], ["FM-1_929", null, "refuse", "unknown"],
  ["ota-FM-1_930", null, "loader", "loader"],
];
{
  const bad = GUARD_CASES.filter(([i, v, verdict, kind]) => { const r = classifyFirmware(i, v); return r.verdict !== verdict || r.kind !== kind; });
  ok(!bad.length, `classifyFirmware: ${GUARD_CASES.length} identity / INFO cases (ChoralRoot's, and CoralFlower over CoralFlower)${bad.length ? ": " + JSON.stringify(bad) : ""}`);
  ok(classifyFirmware("FM-1_930", "CoralFlower 0.1").name === "CoralFlower 0.1", "classifyFirmware: CoralFlower is named by its INFO text");
  ok(classifyFirmware("FM-1_930", null).name === "CoralFlower (an early build without backup support)" && CF_EARLY === classifyFirmware("FM-1_930", null).name,
    "classifyFirmware: FM-1_930 without INFO -> allow, \"CoralFlower (an early build without backup support)\"");
  ok(refusalText("Sloop").includes("then install CoralFlower.") && refusalText("Sloop").includes("https://github.com/Quixotic7/MvaveFM1Unbricker") &&
     !/ChoralRoot/.test(refusalText("Sloop")), "refusalText: names CoralFlower, links the recovery tools");
  const py = spawnSync("python3", ["-c", [
    "import json, sys", `sys.path.insert(0, ${JSON.stringify(join(ROOT, "tools"))})`, "import fm1_install as I",
    "cases = json.loads(sys.argv[1])",
    "print(json.dumps([list(I.classify_firmware(i, v)) for i, v in cases] + [[I.family(f) for f in ('CoralFlower 0.1', 'ChoralRoot 0.2', 'FELUCCA v1.0', 'x')]]))",
  ].join("\n"), JSON.stringify(GUARD_CASES.map(([i, v]) => [i, v]))], { encoding: "utf8" });
  if (py.status !== 0 || !py.stdout) console.log(`fm1_install.py classify_firmware: skipped (python3: ${py.error ? py.error.message : py.stderr.trim().split("\n").pop()})`);
  else {
    const got = JSON.parse(py.stdout), fam = got.pop();
    const want = GUARD_CASES.map(([i, v]) => { const r = classifyFirmware(i, v); return [r.verdict, r.kind, r.name]; });
    ok(JSON.stringify(got) === JSON.stringify(want), "tools/fm1_install.py classify_firmware == fm1ota.js classifyFirmware (verdict, kind, name)");
    ok(fam.join() === "coralflower,choralroot,felucca,other", "tools/fm1_install.py family: coralflower, choralroot, felucca, other");
  }
  ok(backupFamily("CoralFlower 0.1") === "coralflower" && backupFamily("ChoralRoot 0.21") === "choralroot" && backupFamily("FELUCCA v1.0") === "felucca" &&
     backupFamily("MELODEE v1") === "other" && /^coralflower-backup-\d{8}\.json$/.test(backupFileName("CoralFlower 0.1")),
     "fm1backup.js backupFamily: coralflower (coralflower-backup-YYYYMMDD.json), the others as before");
}

/* ------------------------------------------------------------- (b) the shape --- */
const MOD_OPEN = '<script type="module">';
const modSrc = html.slice(html.indexOf(MOD_OPEN) + MOD_OPEN.length, html.indexOf("</script>", html.indexOf(MOD_OPEN)));
const links = html.match(/<link\s[^>]*>/gi) || [];   // the site's own files only: the icons, the font, site.css
ok(count(html, MOD_OPEN) === 1 && count(html, "<script") === 4 && !/<script[^>]*\ssrc=/.test(html) &&
   links.every((l) => /href="(?:[a-z0-9-]+\/)?[a-zA-Z0-9.-]+"/.test(l) && !/:\/\//.test(l)) && html.includes('<link rel="stylesheet" href="site.css">'),
  "page: the module script, the Support script, the page script and the .js flag; links only to this site (site.css, the icons, the font)");
const imports = [...modSrc.matchAll(/^import \{([^}]*)\} from "([^"]+)";$/gm)].map((m) => ({ names: m[1].split(",").map((s) => s.trim()).filter(Boolean), from: m[2] }));
ok(imports.map((i) => i.from).join() === "./fm1pkg.js,./fm1ota.js,./fm1backup.js" && count(modSrc, "import ") === 3,
  "page: imports ./fm1pkg.js, ./fm1ota.js, ./fm1backup.js (the same folder, relative)");
const missingExports = imports.flatMap(({ names, from }) => {
  const src = readFileSync(join(HERE, from), "utf8");
  return names.filter((n) => !new RegExp(`^export (?:async )?(?:function|const|class|let) ${n}\\b`, "m").test(src)).map((n) => `${from}:${n}`);
});
ok(!missingExports.length, `page: every imported name is exported by its module${missingExports.length ? " (missing " + missingExports + ")" : ""}`);
ok(/<title>CoralFlower[^<]*FM-1<\/title>/.test(html) && /<html lang="en">/.test(html) && count(html, "<h1") === 1, "page: title, lang, one h1");
for (const [f, at] of [["fonts/DepartureMono.woff", "Departure Mono"], ["fonts/InterTight-400.woff", "Inter Tight"], ["fonts/InterTight-600.woff", "Inter Tight"]])
  ok(existsSync(join(HERE, f)) && css.includes(`url("${f}")`) && css.includes(`font-family: "${at}"`), `site.css: the font ${f}, from this site`);
ok(html.includes('class="brand"') && html.includes('<span class="brand-a">Coral</span><span class="brand-b">Flower</span>') &&
   ["#features", "#garden", "#safety", "#faq", "play/"].every((h) => html.includes(`<a href="${h}">`)) &&
   ["../#features", "../#garden", "../#safety", "../#faq"].every((h) => emu.includes(`<a href="${h}">`)) && emu.includes('<a href="./" aria-current="page">Play</a>'),
   "page: the CoralFlower name and the same five links on both pages (four sections, Play)");
for (const [name, h] of [["web/index.html", html], ["tools/emu/web/index.html", emu]]) {
  const markup = h.replace(/<script[\s\S]*?<\/script>/g, "");
  const raw = (markup.match(/(?<!&)#[0-9a-fA-F]{3,8}\b/g) || []).filter((c) => !/^#110A02$/i.test(c));   // (theme-color: the bg token's value)
  const styles = [...markup.matchAll(/style="([^"]*)"/g)].map((m) => m[1]).filter((v) => !/^--[a-z-]+: var\(--[a-z0-9-]+\)$/.test(v));
  ok(!/<style/.test(h) && !raw.length && !styles.length && h.includes(name === "web/index.html" ? '<link rel="stylesheet" href="site.css">' : '<link rel="stylesheet" href="../site.css">'),
    `${name}: its look from site.css only (no <style>, no raw colour, style= only sets a token)${raw.length ? " " + raw : ""}${styles.length ? " " + styles : ""}`);
}
{
  const cut = (s, a, b) => { const i = s.indexOf(a); return i < 0 ? null : s.slice(i, s.indexOf(b, i) + b.length); };
  const parts = [
    ["the colour ramp", (s) => cut(s, '<div class="ramp"', "</div>")],
    ["the Support button", (s) => s.split("\n").find((l) => l.includes('id="supportBtn"')).trim().replace(/"\.\.\//g, '"')],
    ["the Support card", (s) => cut(s, '<section class="supportcard"', "</section>")],
    ["the dialog", (s) => cut(s, '<dialog id="donate"', "</dialog>")],
    ["the Support script with WALLETS", (s) => cut(s, "<script>\n/* Support:", "</script>")],
  ];
  for (const [what, f] of parts) { const a = f(emu); ok(a && a.length > 40 && html.includes(a), `page: ${what}, the same as on the emulator page`); }
}
ok(/http-equiv="refresh" content="0; url=\.\.\/#install"/.test(fwd) && fwd.includes('location.replace("../"') && fwd.includes('href="../#install"'),
  "web/install/index.html: the old address forwards to /#install (a script, a refresh, a link)");
ok(!html.includes("FM-1_920") && !/Quixotic7\/ChoralRootFM1/.test(html) && !/snd-|>Sounds<|manager\.html|\/\*(LIB|META)\*\//.test(html) &&
   !/ChoralRoot (is|has|keeps)/.test(html), "page: no ChoralRoot-only parts (its identity, repo, Sounds, manager, placeholders)");
ok(html.includes('href="https://www.m-vave.com/download"') && /at your own risk/.test(html) && /Chrome or Edge/.test(html) && /about a minute/.test(html) &&
   /Don’t unplug/.test(html) && /straight to the computer/.test(html) && /Keep this page open/.test(html),
   "page: own risk, the official firmware from m-vave.com, Chrome or Edge, the cable, keep open, do not unplug, about a minute");
ok(["pkg-file", "pkg-pick", "pkg", "go", "bar", "status", "backup-first", "force-box", "force-risk", "done", "interrupted", "check-go", "unit", "log",
    "backup-go", "restore-file", "restore-go", "dark", "stock", "stock-file", "stock-go", "stock-recovery", "stock-status", "versions", "nomidi",
    "backup-status", "pkg-own", "install", "hero-title"]
   .every((id) => html.includes(`id="${id}"`)) && html.includes('id="backup-first" type="checkbox" checked') && html.includes('id="force-box" hidden') &&
   html.includes('id="pkg-file" type="file" accept=".fwsc" hidden'), "page: the elements (the backup on by default, the override hidden, the .fwsc picker)");

/* ------------------------------------------------------------- (c) texts --- */
const textSrc = modSrc.match(/const TEXT = (\{[\s\S]*?\n\});/);
const TEXT = textSrc ? new Function(`return ${textSrc[1]}`)() : {};
const en = TEXT.en || {};
ok(textSrc && Object.keys(TEXT).join() === "en", "texts: a single `en` table");
const codes = new Set();
for (const m of ota.matchAll(/\bfail\(([^,]+),/g)) for (const s of m[1].matchAll(/"([a-z]+)"/g)) codes.add(s[1]);
for (const c of ["nomidi", "denied", "leave", "error", "start", "verify", "loader", "write", "reboot", "done", "stopped", "cancelled"]) codes.add(c);
const missingC = [...codes].filter((c) => typeof en[c] !== "string");
ok(codes.size > 13 && !missingC.length, `texts: every fm1ota.js error code and step has a text (${codes.size}${missingC.length ? "; missing " + missingC : ""})`);
const keys = [...modSrc.matchAll(/(?:\bsayK|\bt|\btv|\bcoded)\("([A-Za-z]+)"/g)].map((m) => m[1]);
const missingK = keys.filter((k) => typeof en[k] !== "string");
ok(keys.length > 40 && !missingK.length, `texts: every key the script names has a text (${new Set(keys).size} keys${missingK.length ? "; missing " + missingK : ""})`);

/* -------------------------------- (d) packages: a JS port of tools/fm1pkg_make.py --- */
const ascii = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0));
const hexb = (h) => Uint8Array.from(h.match(/../g), (x) => parseInt(x, 16));
const concat = (...a) => { const o = new Uint8Array(a.reduce((n, x) => n + x.length, 0)); let k = 0; for (const x of a) { o.set(x, k); k += x.length; } return o; };
const fillB = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 31 + seed * 7 + 3) & 255);
function encJ(buf, off, size, key = 0xFFFF) {
  for (let i = 0; i < size; i++) { buf[off + i] ^= key & 0xFF; key = ((key * 2) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF; }
}
const sfcJ = (buf, off, size, base, key) => { for (let i = 0; i < size; i += 32) encJ(buf, off + i, Math.min(size - i, 32), key ^ ((off + i - base) >> 2)); };
function entryJ(dcrc, offset, size, flags, resvd, index, name) {
  const e = new Uint8Array(32), v = new DataView(e.buffer);
  v.setUint16(2, dcrc, true); v.setUint32(4, offset >>> 0, true); v.setUint32(8, size >>> 0, true); e[12] = flags; e[13] = resvd; v.setUint16(14, index, true);
  if (typeof name === "string") { const nm = new Uint8Array(16).fill(0xFF); nm.set(ascii(name)); nm[name.length] = 0; e.set(nm, 16); } else e.set(name, 16);
  v.setUint16(0, crc16(e.subarray(2, 32)), true);
  return e;
}
const FLASH_SIZE = 0x93000, APP_SLOT = 0x8DFBC, KEY = 0x980F;
function flashImage(app, key = KEY) {            // fm1pkg_make.flash_image, with stand-ins for the three SDK files
  const spl = fillB(0x400, 11), cfgTool = fillB(0x200, 12), eqb = fillB(0x100, 13);
  const kb = new Uint8Array(34).fill(0x10, 0, 16);
  for (let i = 0; i < 16; i++) kb[16 + i] = kb[15 - i] ^ ((key >> i) & 1 ? 0x00 : 0xFF);
  new DataView(kb.buffer).setUint16(32, crc16(kb.subarray(0, 32)), true);
  const isd = concat(kb, ascii("[FELUCCA]\r\n"));
  const f = new Uint8Array(FLASH_SIZE).fill(0xFF), splOff = 0xA0, isdOff = splOff + spl.length;
  f.set(spl, splOff); f.set(isd, isdOff);
  const hdr = new Uint8Array(32), fh = hexb("20022f104cc900f00f001010a4ff4e5d0b41e0cd781dc856417fb367cf9f");
  hdr.set(fh, 2); new DataView(hdr.buffer).setUint16(0, crc16(fh), true);
  const head = concat(hdr, entryJ(crc16(spl), splOff, spl.length, 0, 0, 0, "uboot.boot"), entryJ(crc16(isd), isdOff, isd.length, 0x02, 0x80, 0, "isd_config.ini"),
    entryJ(0xFFFF, 0x4000, 0xFFFFFFFF, 0x81, 0xFF, 0, "app_dir_head"), entryJ(0xFFFF, 0xFF000, 0x1000, 0x12, 0x01, 0xFFFF, "key_mac"));
  for (let o = 0; o < head.length; o += 32) encJ(head, o, 32);
  f.set(head, 0);
  const a = new Uint8Array(APP_SLOT).fill(0xFF); a.set(app);
  const blk = 0x120 + APP_SLOT + cfgTool.length, area = new Uint8Array(blk).fill(0xFF);
  area.set(a, 0x120); area.set(cfgTool, 0x120 + APP_SLOT);
  area.set(concat(entryJ(crc16(a), 0x120, APP_SLOT, 0x82, 0xFF, 0, "app.bin"), entryJ(crc16(cfgTool), 0x120 + APP_SLOT, cfgTool.length, 0x82, 0xFF, 0, "cfg_tool.bin"),
    entryJ(0xFFFF, 0x93000, 352256, 0x12, 0x80, 0, hexb("564d00ffffffffffffffffff00800000")), entryJ(0xFFFF, 0, FLASH_SIZE, 0x92, 0x82, 0, hexb("5052435400ffffffffffffff434f4445")),
    entryJ(0xFFFF, 0xE9000, 0x1000, 0x92, 0x80, 0, hexb("4254494600ffffffffffffff4155544f")), entryJ(0xFFFF, 0xEA000, 0x12000, 0x92, 0x81, 1, "USR")), 32);
  area.set(entryJ(crc16(area.subarray(32, blk)), 0x02000120, blk, 0x83, 0xFF, 0, "app_area_head"), 0);
  const cfgBody = concat(entryJ(crc16(eqb), 0x40, eqb.length, 0x82, 0xFF, 1, "eq_cfg_hw.bin"), eqb);
  const region = concat(area, entryJ(crc16(cfgBody), 0x20, 0x20 + cfgBody.length, 0x83, 0xFF, 1, "cfg"), cfgBody);
  sfcJ(region, 0, region.length, 0, key);
  f.set(region, 0x4000);
  return f;
}
function ufw(flash, otaBin, product) {           // fm1pkg_make.ufw: the UFW container and the identity markers
  const files = [[0, "flash.bin", flash], [100, "ota.bin", otaBin]];
  let off = 0x400; const ents = [];
  files.forEach(([typ, name, d], i) => {
    off = (off + 0xFF) & ~0xFF;
    const e = new Uint8Array(0x50), v = new DataView(e.buffer);
    v.setUint16(0, typ, true); v.setUint16(2, i, true); v.setUint16(4, crc16(d), true); v.setUint32(8, off, true); v.setUint32(12, d.length, true);
    v.setUint32(16, (d.length + 31) & ~31, true); e.set(ascii(name), 0x40);
    ents.push([e, off, d]); off += d.length;
  });
  const hdr = new Uint8Array(0x40), hv = new DataView(hdr.buffer);
  hv.setUint32(4, off, true); hv.setUint16(8, files.length, true); hv.setUint16(10, 4, true); hv.setUint16(12, 0x200, true); hdr.set(ascii("AC791N"), 16);
  const lst = concat(...ents.map(([e]) => { const x = e.slice(); encJ(x, 0, 0x50); return x; }));
  hv.setUint16(2, crc16(lst), true); hv.setUint16(0, crc16(hdr.subarray(2, 0x40)), true); encJ(hdr, 0, 0x40);
  const logical = new Uint8Array(off).fill(0xFF);
  logical.set(hdr, 0); logical.set(lst, 0x40);
  for (const [, o, d] of ents) logical.set(d, o);
  const raw = new Uint8Array(off + 20);
  for (let i = 0; i < 20; i++) {
    raw.set(logical.subarray(i * 0x2F, (i + 1) * 0x2F), i * 0x30);
    raw[i * 0x30 + 0x2F] = i < product.length ? (product.charCodeAt(i) + i + 1) & 0xFF : 0x7D;
  }
  raw.set(logical.subarray(20 * 0x2F), 20 * 0x30);
  return raw;
}
const makePkg = ({ product = "FM-1_930", marker = true, seed = 0 } = {}) =>
  ufw(flashImage(fillB(0x6000, seed + 1)), concat(fillB(0x700, seed + 2), marker ? ascii("FELUCCA-LOADER-1") : fillB(16, 9), fillB(0x300, seed + 3)), product);

const good = makePkg(), good2 = makePkg({ seed: 5 });
{
  let parsed = null;
  try { parsed = new Package(good); } catch (e) { console.log(String(e)); }
  ok(parsed && productOf(good) === "FM-1_930" && parsed.flashSize === FLASH_SIZE && parsed.appBase === 0x4000 && parsed.app.size === APP_SLOT &&
     parsed.entries.some((e) => e.type === 100 && e.name === "ota.bin"), `test package (fm1pkg_make.py in JS): ${good.length} B, FM-1_930, parses as an FM-1 package`);
}

/* ---------------------------------------------- (e) the page, run against a DOM stub --- */
const stripModule = (src) => src.replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
const lib = ["fm1pkg.js", "fm1ota.js", "fm1backup.js"].map((f) => stripModule(readFileSync(join(HERE, f), "utf8"))).join("\n");
const code = lib + "\n" + stripModule(modSrc);
let compiled = false;
try { new vm.Script(code, { filename: "installer.js" }); compiled = true; } catch (e) { console.log(String(e)); }
ok(compiled && !/^\s*(import|export)\s/m.test(code), "page: its module script with the three libraries inlined compiles (no name clash)");

class El {
  constructor(attrs) {
    this.id = attrs.id; this.textContent = ""; this.disabled = "disabled" in attrs; this.value = ""; this.files = [];
    this.checked = "checked" in attrs; this.open = "open" in attrs; this.dataset = {}; this.style = {}; this.l = {}; this.clicks = 0;
    this.hidden = "hidden" in attrs; this.innerHTML = ""; this.className = attrs.cls || ""; this.scrolled = 0;
  }
  getBoundingClientRect() { return { top: 5000, bottom: 5030 }; }
  scrollIntoView() { this.scrolled++; }
  click() { this.clicks++; }
  addEventListener(type, f) { this.l[type] = f; }
  fire(type, ev = {}) { return this.l[type] ? this.l[type]({ type, ...ev }) : undefined; }
}
function makeDom() {
  const byId = {};
  const page = html.slice(0, html.indexOf(MOD_OPEN));
  for (const m of page.matchAll(/<[a-z][a-z0-9]*\b([^>]*)>/g)) {
    const a = m[1], attrs = {}, id = /\sid="([^"]+)"/.exec(a), cls = /\sclass="([^"]+)"/.exec(a);
    if (!id) continue;
    attrs.id = id[1]; if (cls) attrs.cls = cls[1];
    for (const flag of ["disabled", "hidden", "checked", "open"]) if (new RegExp(`\\s${flag}(\\s|=|$)`).test(a)) attrs[flag] = true;
    byId[id[1]] = new El(attrs);
  }
  return byId;
}
const fastTimeout = (f, ms = 0, ...a) => setTimeout(f, ms >= 100 ? ms / 10 : ms, ...a);
function runPage({ navigator = {}, fetch, confirm = () => true, prelude = "", extra = {} } = {}) {
  const byId = makeDom();
  const win = { l: {}, innerHeight: 800, addEventListener(type, f) { this.l[type] = f; } };
  const confirms = [], downloads = [], fetched = [];
  const document = {
    l: {}, body: { append() {} },
    createElement: (tag) => ({ tag, href: "", download: "", remove() {}, click() { downloads.push({ name: this.download, href: this.href }); } }),
    getElementById: (id) => byId[id] || null,
    addEventListener(type, f) { this.l[type] = f; },
  };
  const ctx = {
    document, window: win, navigator,
    fetch: async (url, o) => { fetched.push(url); return (fetch || notFound)(url, o); },
    confirm: (msg) => { confirms.push(msg); return confirm(msg); },
    URL, Blob, crypto: globalThis.crypto, TextEncoder, TextDecoder, console, btoa, atob, DataView,
    setTimeout: fastTimeout, clearTimeout, setInterval, clearInterval, ...extra,
  };
  const src = prelude ? code.replace(lib, () => lib + "\n" + prelude) : code;
  let error = null;
  try { vm.runInNewContext(src, ctx, { filename: "installer.js" }); } catch (e) { error = e; }
  return { $: byId, win, document, confirms, downloads, fetched, error, ready: win.installerReady || Promise.resolve() };
}
const notFound = async () => ({ ok: false, status: 404, json: async () => { throw new Error("404"); }, arrayBuffer: async () => new ArrayBuffer(0) });
const fileOf = (name, bytes) => ({ name, size: bytes.length, arrayBuffer: async () => bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length) });
async function choose(p, name, bytes) {          // "Choose a .fwsc file" and a file picked
  await p.$["pkg-pick"].fire("click");
  p.$["pkg-file"].files = [fileOf(name, bytes)];
  await p.$["pkg-file"].fire("change");
}

/* a simulated FM-1 on WebMIDI (ChoralRoot's FakeFM1; bk(identity, boots) gives the firmware's backup side) */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
class FakeFM1 {
  constructor(image, { identity = "FM-1_015", loader = "ota-FM-1_930", final = "FM-1_930", onServe = null, bk = null } = {}) {
    this.image = image; this.served = 0; this.bad = 0; this.upgrades = 0; this.bkFrames = 0; this.bkCmds = []; this.boots = 0;
    this.loader = loader; this.final = final; this.onServe = onServe; this.bk = bk;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, identity.startsWith("ota-") ? "Felucca Update" : "CoralFlower FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = []; this.boots++;
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (d[0] === 0xF0 && d[1] === 0x7D && d[2] === 0x46 && d[3] === 0x4C) {   // the backup protocol (fm1backup.js)
      this.bkFrames++; this.bkCmds.push(d[4]);
      const side = this.bk && this.bk(this.identity, this.boots), r = side && side.handle(d[4], d.slice(5, -1));
      if (r) this.tx([0xF0, 0x7D, 0x46, 0x4C, d[4], ...r, 0xF7]);
      return;
    }
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      this.tx([0xF0, ...pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.upgrades++;
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1)), [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null; this.served++;
      if (this.onServe) this.onServe(this.served);
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loader, "Felucca Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.final, "CoralFlower FM-1"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}
// a firmware's backup side (cr_backup.c / Felucca's editor_backup.c): INFO, LIST, GET, PUT, RESTART
function backupSide(version, ids, objs = []) {
  const b = { objs: new Map(objs), log: [], staged: null, restarts: 0 };
  b.handle = (cmd, a) => {
    if (cmd === 1) return [...new TextEncoder().encode(version), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3];
    if (cmd === 65) {
      const o = [1, 0, ids.length];
      for (const id of ids) { const v = b.objs.get(id) || new Uint8Array(0); o.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return o;
    }
    if (cmd === 66) { const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = b.objs.get(id); return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))]; }
    if (cmd === 67) {
      const [op, id] = a;
      if (op === 0) { b.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, ids.includes(id) ? 0 : 1]; }
      if (op === 1) { b.staged.bytes.push(...bkUnpack(a.slice(7), Math.min(256, b.staged.size - bkR32(a, 2)))); return [op, id, 0]; }
      if (op === 2) { const v = Uint8Array.from(b.staged.bytes); const rc = bkCrc(v) === b.staged.crc ? 0 : 2; if (!rc) { b.objs.set(id, v); b.log.push(id); } return [op, id, rc]; }
      return [op, id, 0];
    }
    if (cmd === 72 && /^(ChoralRoot|CoralFlower)/.test(version)) { b.restarts++; return [0]; }
    return null;
  };
  return b;
}
const fill = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 31 + seed) & 255);
const per = (n, magic, seed) => { const v = fill(n, seed); new DataView(v.buffer).setUint32(0, magic, true); return v; };
const feluccaData = () => [[0, fill(3584, 1)], [1, per(572, 0x50455234, 2)], [2, fill(3584, 3)], [6, fill(3080, 4)], [8, fill(3472, 5)]];
const crData = () => [[1, per(764, 0x50455235, 6)], [6, fill(3080, 7)], [9, fill(3536, 8)], [40, fill(46, 9)]];
// CoralFlower 0.3.1: a sound bank, the settings, the loop and the sequencer's store in its six parts (5 x 3840 + 1136 B)
const cfData = () => [[6, fill(3080, 10)], [30, fill(200, 11)], [31, fill(64, 12)],
  ...[50, 51, 52, 53, 54, 55].map((id, k) => [id, fill(k < 5 ? 3840 : 1136, 13 + k)])];
const midiOf = (dev) => ({ requestMIDIAccess: async () => dev.access });
const image = logicalImage(good);
// a page with a checked file, ready to install (the default: the well-formed package)
async function readyPage(dev, opts = {}) {
  const p = runPage({ navigator: midiOf(dev), ...opts });
  await p.ready;
  await choose(p, "coralflower-test.fwsc", opts.pkg || good);
  return p;
}

// no Web MIDI: the page runs, says why, Install stays off (a file is still checked)
{
  const p = runPage({ navigator: {} });
  await p.ready;
  ok(!p.error, `no Web MIDI: the page's top-level code runs${p.error ? " (" + p.error.stack + ")" : ""}`);
  ok(p.$.status.textContent === en.nomidi && p.$.nomidi.hidden === false && p.$.go.disabled && p.$["check-go"].disabled && p.$["backup-go"].disabled,
    "no Web MIDI: the notice and the status say why; Install, the check and Back up are off");
  await choose(p, "coralflower-test.fwsc", good);
  ok(p.$.pkg.className === "pkg ok" && p.$.go.disabled, "no Web MIDI: a good file is still checked (ready), Install stays off");
}

// no releases.json: only a file; nothing to install until one is checked
{
  const dev = new FakeFM1(image);
  const p = runPage({ navigator: midiOf(dev) });
  await p.ready;
  ok(!p.error && p.fetched.join() === "firmware/releases.json" && p.$.versions.hidden && !p.$["rel-none"].hidden && p.$["rel-some"].hidden &&
     p.$.go.disabled && p.$.go.textContent === en.installNone && p.$.pkg.textContent === "" && p.$.ver.textContent === en.noRelease, "no releases.json (404): the file only, Install off and named \"Install CoralFlower\"");
  ok(/releases\.json: HTTP 404/.test(p.$.log.textContent), "no releases.json: the log says so");
  await p.$["pkg-pick"].fire("click");
  ok(p.$["pkg-file"].clicks === 1, "\"Choose a .fwsc file\" opens the file picker");
  p.$["pkg-file"].files = [fileOf("coralflower-0.2-test.fwsc", good)];
  await p.$["pkg-file"].fire("change");
  ok(p.$.pkg.className === "pkg ok" && p.$.pkg.textContent === en.pkgOk.replace("{name}", "coralflower-0.2-test.fwsc") &&
     p.$["pkg-sum"].textContent === "SHA-256 " + await sha256(good) && p.$["pkg-name"].textContent === "coralflower-0.2-test.fwsc",
     "file: a well-formed FM-1_930 package -> ready, checked, its SHA-256 shown");
  ok(!p.$.go.disabled && p.$.go.textContent === "Install on your FM-1" && p.$.ver.textContent === "coralflower-0.2-test.fwsc", "file: Install enabled, named after the file");
  // refusals: each leaves Install off, and says why
  const refusals = [
    ["another identity (ChoralRoot's FM-1_920)", makePkg({ product: "FM-1_920" }), "pkgIdentity", "FM-1_920"],
    ["the official firmware's identity (FM-1_015)", makePkg({ product: "FM-1_015" }), "pkgStock", "FM-1_015"],
    ["no Felucca loader in ota.bin", makePkg({ marker: false }), "pkgNoLoader", ""],
    ["a damaged flash.bin (one bit)", (() => { const b = good.slice(); b[0x3000] ^= 1; return b; })(), "pkgDamaged", "flash.bin CRC mismatch"],
    ["a damaged ota.bin (one bit)", (() => { const b = good.slice(); b[b.length - 0x100] ^= 1; return b; })(), "pkgDamaged", "ota.bin CRC mismatch"],
    ["a file cut short", good.slice(0, 0x50000), "pkgDamaged", ""],
    ["not a package (text)", ascii("hello ".repeat(400)), "pkgNotFm1", ""],
    ["an empty file", new Uint8Array(0), "pkgNotFm1", ""],
  ];
  for (const [what, bytes, key, detail] of refusals) {
    await choose(p, "candidate.fwsc", good);     // (a good one first: the refusal must take Install away again)
    const was = !p.$.go.disabled;
    await choose(p, "x.fwsc", bytes);
    const txt = p.$.pkg.textContent;
    ok(was && p.$.go.disabled && p.$.go.textContent === en.installNone && p.$.pkg.className === "pkg bad" && txt.startsWith(en[key].split("{name}").join("x.fwsc").split(" (")[0].split(" It is")[0].split("{id}")[0]) &&
       (!detail || txt.includes(detail)) && p.$["pkg-sum"].textContent === "" && dev.upgrades === 0,
       `file refused: ${what} -> ${key}, Install off again`);
  }
}

// installs from a file
{
  // over the stock firmware: it cannot be backed up (asked once), then installed, identity FM-1_930
  const dev = new FakeFM1(image);
  const p = await readyPage(dev);
  let guarded = null;
  dev.onServe = (n) => { if (n === 2) { let prevented = false; p.win.l.beforeunload({ preventDefault: () => { prevented = true; }, returnValue: "" }); guarded = prevented && p.$.go.disabled && p.$["pkg-pick"].disabled; } };
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && p.$.bar.value === 100 && dev.bad === 0 && dev.identity === "FM-1_930",
    `install over stock: -> loader -> FM-1_930, status Done, bar 100 (${dev.served} reads, all bytes right)`);
  ok(guarded === true, "install: Install and the file picker locked, the page guarded against closing while writing");
  ok(p.confirms.length === 1 && p.confirms[0].startsWith(en.noBackupConfirm) && !p.downloads.length && !dev.bkCmds.includes(65) && p.$["backup-note"].hidden,
    "install over stock: no INFO, so no backup tried -> asked once, as before");
  ok(p.$.done.hidden === false && p.$["done-line"].textContent === en.dnPlain && p.$.interrupted.hidden && !p.$.go.disabled,
    "install over stock: the done panel (no INFO answer afterwards: \"CoralFlower is installed\"), unlocked");
  ok(/^start /m.test(p.$.log.textContent) && /^write \d+/m.test(p.$.log.textContent) && /install: coralflower-test\.fwsc/.test(p.$.log.textContent), "install: the log lists the steps");
}
{
  // CoralFlower over CoralFlower: backed up (coralflower-backup-*.json), no restore offered, the new version read back
  const before = backupSide("CoralFlower 0.3.1", CF_BACKUP_IDS, cfData()), after = backupSide("CoralFlower 0.2", CR_BACKUP_IDS, crData());
  const dev = new FakeFM1(image, { identity: "FM-1_930", bk: (id, boots) => (id.startsWith("ota-") ? null : boots < 3 ? before : after) });
  const p = await readyPage(dev);
  await p.$.go.fire("click");
  ok(p.downloads.length === 1 && /^coralflower-backup-\d{8}\.json$/.test(p.downloads[0].name) && !p.confirms.length && !before.log.length && !after.log.length,
    `CoralFlower over CoralFlower: backed up first (${p.downloads[0]?.name}), no question, no restore`);
  ok(/^backup CoralFlower 0\.3\.1: user sounds 1-16, CoralFlower settings, CoralFlower loop, CoralFlower patterns and song$/m.test(p.$.log.textContent),
    "CoralFlower over CoralFlower: the backup holds the sequencer's six parts, named once in the log");
  ok(p.$.status.textContent === en.done && dev.identity === "FM-1_930" && dev.bad === 0 && p.$["done-line"].textContent === "Done. Your FM-1 now runs CoralFlower 0.2.",
    "CoralFlower over CoralFlower: installed, and the new version read back (\"now runs CoralFlower 0.2\")");
  ok(/running: FM-1_930 CoralFlower 0\.3\.1 -> allow \(coralflower\)/.test(p.$.log.textContent), "CoralFlower over CoralFlower: classified allow (coralflower) in the log");
}
{
  // over ChoralRoot: allowed, backed up, no restore offered (its data stays in the flash, CoralFlower uses it)
  const cr = backupSide("ChoralRoot 0.21", CR_BACKUP_IDS, crData());
  const dev = new FakeFM1(image, { identity: "FM-1_920", bk: (id) => (id === "FM-1_920" ? cr : null) });
  const p = await readyPage(dev);
  await p.$.go.fire("click");
  ok(p.downloads.length === 1 && /^choralroot-backup-/.test(p.downloads[0].name) && !p.confirms.length && p.$.status.textContent === en.done && dev.identity === "FM-1_930",
    "over ChoralRoot: backed up, installed, no restore offered");
}
{
  // over Felucca 1.0: the backup first, then CoralFlower, then the backup offered back, restored, RESTART
  const fel = backupSide("FELUCCA 1.0", BACKUP_IDS, feluccaData()), cf = backupSide("CoralFlower 0.2", CR_BACKUP_IDS);
  const dev = new FakeFM1(image, { identity: "FM-1_910", bk: (id) => (id === "FM-1_910" ? fel : id === "FM-1_930" ? cf : null) });
  const p = await readyPage(dev);
  await p.$.go.fire("click");
  ok(p.downloads.length === 1 && /^felucca-backup-\d{8}\.json$/.test(p.downloads[0].name) && p.confirms.length === 1 && p.confirms[0] === en.restoreOffer,
    "over Felucca: the backup saved first, then the restore offered");
  ok(cf.log.join() === "6,7,8,1" && cf.objs.get(6).every((v, i) => v === feluccaData()[3][1][i]) && cf.restarts === 1 && p.$.status.textContent.startsWith(en.restored) &&
     p.$.done.hidden === false, "over Felucca: settings, banks, FM6 bank restored on CoralFlower, then RESTART; the done panel");
}
{
  // an early CoralFlower build (FM-1_930, no INFO: no backup protocol): allowed; the backup skipped with a note, not failed
  const dev = new FakeFM1(image, { identity: "FM-1_930" });
  const p = await readyPage(dev);
  await p.$.go.fire("click");
  ok(dev.identity === "FM-1_930" && dev.bad === 0 && dev.upgrades === 2 && p.$.status.textContent === en.done && p.$.done.hidden === false && p.$["force-box"].hidden !== false,
    "early CoralFlower (FM-1_930, no INFO): allowed and installed");
  ok(!p.confirms.length && !p.downloads.length && !dev.bkCmds.includes(65) && p.$["backup-note"].hidden === false && p.$["backup-note"].textContent === en.noBackupCf &&
     /backup: skipped/.test(p.$.log.textContent) && /running: FM-1_930 -> allow \(coralflower\)/.test(p.$.log.textContent),
     "early CoralFlower: the backup step skipped (no LIST, no question, no file), the note shown");
  ok(/can’t make a backup/.test(en.noBackupCf) && /sounds and settings stay safe/.test(en.noBackupCf) && /only rewrites the firmware/.test(en.noBackupCf),
    "early CoralFlower: the note says the backup is impossible and why the update keeps the saved sounds");
  const again = await readyPage(new FakeFM1(image, { identity: "FM-1_930" }));
  again.$["backup-first"].checked = false;
  await again.$.go.fire("click");
  ok(again.$["backup-note"].hidden === true && again.$.status.textContent === en.done, "early CoralFlower, backup unticked: installed, no note");
  const odd = await readyPage(new FakeFM1(image, { identity: "FM-1_931" }));
  await odd.$.go.fire("click");
  ok(odd.$.status.textContent === refusalText("an unknown Felucca-based firmware (FM-1_931)") && odd.$["force-box"].hidden === false && odd.$["backup-note"].hidden,
    "FM-1_931 without INFO: still refused (the override box shown, nothing written)");
}
{
  // the backup box unticked: no backup request beyond INFO
  const fel = backupSide("FELUCCA 1.0", BACKUP_IDS, feluccaData());
  const dev = new FakeFM1(image, { identity: "FM-1_910", bk: (id) => (id === "FM-1_910" ? fel : null) });
  const p = await readyPage(dev);
  p.$["backup-first"].checked = false;
  await p.$.go.fire("click");
  ok(dev.bkCmds.join() === "1,1" && !p.downloads.length && !p.confirms.length && p.$.status.textContent === en.done,
    "backup unticked: installed; only INFO asked (to classify before, to read back after), no backup, no question");
}
{
  // no backup possible, declined: nothing written
  const dev = new FakeFM1(image);
  const p = await readyPage(dev, { confirm: () => false });
  await p.$.go.fire("click");
  ok(p.confirms.length === 1 && dev.upgrades === 0 && p.$.status.textContent === en.cancelled && !p.$.go.disabled, "no backup, declined: nothing written, unlocked");
}

// the firmware check before an install (ChoralRoot's docs/INSTALL-COMPAT.md): refusals and the override
{
  const over = async (identity, version, { force = false, answer = true } = {}) => {
    const side = version ? backupSide(version, BACKUP_IDS, feluccaData()) : null;
    const dev = new FakeFM1(image, { identity, bk: (id) => (id === identity ? side : null) });
    const p = await readyPage(dev, { confirm: () => answer });
    p.$["backup-first"].checked = false;
    await p.$.go.fire("click");
    if (force) { p.$["force-risk"].checked = true; await p.$.go.fire("click"); }
    return { p, dev };
  };
  let { p, dev } = await over("FM-1_900", "FELUCCA SLOOP 2.2");
  ok(p.$.status.textContent === refusalText("Sloop") && dev.upgrades === 0 && !p.downloads.length && !p.confirms.length && p.$["force-box"].hidden === false && !p.$.go.disabled,
    "guard: over Sloop -> refused (nothing written, no question), the override box appears");
  ({ p, dev } = await over("FM-1_900", "FELUCCA SLOOP 2.2", { force: true, answer: false }));
  ok(p.confirms.length === 1 && p.confirms[0] === en.forceConfirm.replace("{name}", "Sloop") && dev.upgrades === 0 && p.$.status.textContent === en.cancelled,
    "guard: override ticked, the second confirm declined -> nothing written");
  ({ p, dev } = await over("FM-1_900", "FELUCCA SLOOP 2.2", { force: true }));
  ok(dev.identity === "FM-1_930" && dev.bad === 0 && p.$.log.textContent.includes("override: installing over Sloop"), "guard: override ticked and confirmed -> installed over Sloop");
  for (const [identity, version, name] of [["FM-1_909", "FELUCCA 0.9-BETA", "a Felucca beta or a firmware based on one (FELUCCA 0.9-BETA)"],
    ["FM-1_000", null, "Sloop's rescue mode (FM-1_000)"], ["FM-1_931", null, "an unknown Felucca-based firmware (FM-1_931)"]]) {
    ({ p, dev } = await over(identity, version));
    ok(p.$.status.textContent === refusalText(name) && dev.upgrades === 0, `guard: over ${identity} ${version || "(no INFO)"} -> refused`);
  }
  for (const [identity, version] of [["FM-1_910", "FELUCCA v1.0.1"], ["FM-1_90111", "MELODEE v0.11.1"], ["FM-1_015", null]]) {
    ({ p, dev } = await over(identity, version));
    ok(dev.identity === "FM-1_930" && p.$.status.textContent === en.done && p.$["force-box"].hidden !== false, `guard: over ${version || "the stock " + identity} -> installed, no override shown`);
  }
}

// resume: the FM-1 is already in update mode
{
  const cf = backupSide("CoralFlower 0.2", CR_BACKUP_IDS);
  const dev = new FakeFM1(image, { identity: "ota-FM-1_930", bk: (id) => (id === "FM-1_930" ? cf : null) });
  const p = await readyPage(dev);
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.done && dev.bad === 0 && dev.identity === "FM-1_930" && !p.confirms.length && !p.downloads.length &&
     p.$["done-line"].textContent === "Done. Your FM-1 now runs CoralFlower 0.2.", "resume: update mode -> the write finished, FM-1_930, no question, the version read back");
}
// interrupted while writing: the guidance
{
  const dev = new FakeFM1(image);
  dev.onServe = () => { if (dev.identity.startsWith("ota-") && dev.served >= 6) dev.input.state = dev.output.state = "disconnected"; };
  const p = await readyPage(dev);
  p.$["backup-first"].checked = false;
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.lost && p.$.interrupted.hidden === false && p.$.done.hidden && p.$.bar.value > 0 && p.$.bar.value < 100 && !p.$.go.disabled,
    `interrupted: the cable lost while writing -> lost, the guidance shown, bar part way (${Math.round(p.$.bar.value)})`);
  // the page script (the last one): a link to a folded tool opens it. Run alone, against a small stub
  const pageSrc = html.slice(html.lastIndexOf("<script>") + 8, html.lastIndexOf("</script>"));
  const dark = { open: false, parentElement: null }; dark.closest = (sel) => (sel === "details" ? dark : null);
  const doc = { l: {}, getElementById: (id) => (id === "dark" ? dark : { disabled: true, focus() {}, scrollIntoView() {} }),
                querySelectorAll: () => [], querySelector: () => null, addEventListener(type, f) { this.l[type] = f; } };
  vm.runInNewContext(pageSrc, { document: doc, location: { hash: "" }, addEventListener() {}, window: {}, decodeURIComponent });
  doc.l.click({ target: { closest: (sel) => (sel === 'a[href^="#"]' ? { getAttribute: () => "#dark" } : null) } });
  ok(html.includes('<a href="#dark">Device not found</a>') && dark.open === true, "interrupted: its link to \"Device not found\" opens that help (the page script)");
}
// errors map to their texts
{
  let p = await readyPage({ access: { inputs: new Map(), outputs: new Map() } });
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.notfound && !p.$.go.disabled, "error: no FM-1 -> notfound, Install unlocked");
  p = await readyPage(new FakeFM1(image, { identity: "FM-2_001" }));
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.model, "error: another device -> model");
  p = await readyPage(new FakeFM1(image, { final: "FM-1_015" }));
  p.$["backup-first"].checked = false;
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.mismatch + "FM-1_015" && p.$.interrupted.hidden, "error: another identity after the write -> mismatch + the identity");
  p = runPage({ navigator: { requestMIDIAccess: async (o) => { if (o && o.sysex) throw new Error("SecurityError"); return {}; } } });
  await p.ready;
  await choose(p, "c.fwsc", good);
  await p.$.go.fire("click");
  ok(p.$.status.textContent === en.denied && !p.$.go.disabled, "error: SysEx refused -> denied, Install unlocked");
}

// "What is on my FM-1?"
{
  const ask = async (dev) => { const p = runPage({ navigator: midiOf(dev) }); await p.ready; await p.$["check-go"].fire("click"); return p.$.unit; };
  let u = await ask(new FakeFM1(image, { identity: "FM-1_930", bk: () => backupSide("CoralFlower 0.1", CR_BACKUP_IDS) }));
  ok(!u.hidden && u.textContent === en.cnCf.replace("{name}", "CoralFlower 0.1") && u.className === "unit", "check: CoralFlower -> its version, an update keeps what is saved");
  u = await ask(new FakeFM1(image, { identity: "FM-1_920", bk: () => backupSide("ChoralRoot 0.21", CR_BACKUP_IDS) }));
  ok(u.textContent === en.cnOther.replace("{name}", "ChoralRoot 0.21"), "check: ChoralRoot -> CoralFlower can be installed over it");
  u = await ask(new FakeFM1(image, { identity: "FM-1_900", bk: () => backupSide("FELUCCA SLOOP 2.2", BACKUP_IDS) }));
  ok(u.textContent === refusalText("Sloop") && u.className === "unit bad", "check: Sloop -> the refusal");
  u = await ask(new FakeFM1(image, { identity: "FM-1_930" }));
  ok(u.textContent === en.cnCf.replace("{name}", "CoralFlower (an early build without backup support)") && u.className === "unit",
    "check: an early CoralFlower (no INFO) -> named as such, allowed");
  u = await ask(new FakeFM1(image, { identity: "ota-FM-1_930" }));
  ok(u.textContent === en.cnLoader.replace("{identity}", "ota-FM-1_930"), "check: update mode -> finish the write with Install");
  u = await ask({ access: { inputs: new Map(), outputs: new Map() } });
  ok(u.textContent === en.notfound && u.className === "unit bad", "check: no FM-1 -> notfound");
}

// the versions of firmware/releases.json: chips, the newest loaded and sha256-checked, a file still possible
{
  const pkg01 = good2;
  const list = (sha02) => ({ releases: [
    { version: "0.2", date: "2026-11-02", title: "Corals", sha256: sha02, bullets: ["Coral growth follows the chord."], notes: ["Updating keeps your sounds."] },
    { version: "0.1", date: "2026-10-20", sha256: await_sha01 },
    { version: "0.0", date: "2026-10-01" },                      // (no sha256: never offered)
  ] });
  const await_sha01 = await sha256(pkg01);
  const site = (sha02, files = { "firmware/coralflower-0.2.fwsc": good, "firmware/coralflower-0.1.fwsc": pkg01 }) => async (url) => {
    if (url === "firmware/releases.json") return { ok: true, status: 200, json: async () => list(sha02) };
    const b = files[url];
    return b ? { ok: true, status: 200, arrayBuffer: async () => b.buffer.slice(b.byteOffset, b.byteOffset + b.length) } : notFound();
  };
  const cf = backupSide("CoralFlower 0.1", CR_BACKUP_IDS, crData());
  const dev = new FakeFM1(image, { identity: "FM-1_930", bk: () => cf });
  let p = runPage({ navigator: midiOf(dev), fetch: site(await sha256(good)) });
  await p.ready;
  const chips = p.$.versions.innerHTML;
  ok(!p.$.versions.hidden && p.$["rel-none"].hidden && !p.$["rel-some"].hidden && count(chips, 'class="chip"') === 2 && chips.includes('data-ver="0.2" aria-pressed="true"') &&
     chips.includes("Newest") && chips.includes('data-ver="0.1" aria-pressed="false"') && !chips.includes('data-ver="0.0"'),
     "releases: two chips (the entry without a SHA-256 left out), the newest chosen");
  ok(p.$.pkg.textContent === en.pkgRelOk.replace("{version}", "0.2") && p.$.pkg.className === "pkg ok" && !p.$.go.disabled && p.$.go.textContent === "Install on your FM-1" && p.$.ver.textContent.startsWith("CoralFlower 0.2") &&
     p.fetched.includes("firmware/coralflower-0.2.fwsc"), "releases: the newest fetched from firmware/, sha256 and package checked, Install CoralFlower 0.2");
  ok(p.$.notes.innerHTML.includes("Coral growth follows the chord.") && p.$.notes.innerHTML.includes('href="firmware/coralflower-0.2.fwsc" download') &&
     p.$.notes.innerHTML.includes("2 November 2026"), "releases: the chosen version's notes, date and download link");
  await p.$.versions.fire("click", { target: { closest: () => ({ dataset: { ver: "0.1" }, disabled: false }) } });
  ok(p.$.ver.textContent.startsWith("CoralFlower 0.1") && p.$.versions.innerHTML.includes('data-ver="0.1" aria-pressed="true"') && p.$.pkg.className === "pkg ok",
    "releases: an older chip chosen -> its own package, checked");
  await choose(p, "candidate.fwsc", good);
  ok(p.$.ver.textContent === "candidate.fwsc" && !p.$.versions.innerHTML.includes('aria-pressed="true"'), "releases: a file chosen instead -> no chip pressed, Install names the file");
  await p.$.versions.fire("click", { target: { closest: () => ({ dataset: { ver: "0.2" }, disabled: false }) } });
  await p.$.go.fire("click");
  ok(dev.identity === "FM-1_930" && dev.bad === 0 && p.$.status.textContent === en.done && /install: CoralFlower 0\.2 \(coralflower-0\.2\.fwsc\)/.test(p.$.log.textContent),
    "releases: back to 0.2, installed from the site's package");
  p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: site("0".repeat(64)) });
  await p.ready;
  ok(p.$.pkg.textContent === en.pkgBadHash.replace("{version}", "0.2") && p.$.pkg.className === "pkg bad" && p.$.go.disabled, "releases: a package whose SHA-256 is not the listed one -> refused, Install off");
  p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: site(await sha256(good), {}) });
  await p.ready;
  ok(p.$.pkg.className === "pkg bad" && p.$.pkg.textContent.includes("HTTP 404") && p.$.go.disabled, "releases: the package missing on the site -> said, Install off");
  p = runPage({ navigator: midiOf(new FakeFM1(image)), fetch: async (url) => ({ ok: true, status: 200, json: async () => { throw new SyntaxError("Unexpected token <"); } }) });
  await p.ready;
  ok(p.$.versions.hidden && !p.$["rel-none"].hidden && p.$.go.disabled, "releases: a releases.json that is not JSON -> the file only");
}

// no Web MIDI and a hosted version: the version is named, its package is not downloaded
{
  const list = { releases: [{ version: "0.2", sha256: "a".repeat(64), date: "2026-10-15" }] };
  const p = runPage({ navigator: {}, fetch: async (url) => (url === "firmware/releases.json" ? { ok: true, status: 200, json: async () => list } : notFound()) });
  await p.ready;
  ok(p.fetched.join() === "firmware/releases.json" && p.$.ver.textContent.startsWith("CoralFlower 0.2") && p.$.go.disabled && p.$.status.textContent === en.nomidi,
    "no Web MIDI: the newest version named, its package not downloaded, Install off");
}

// Back up and Restore
{
  const cf = backupSide("CoralFlower 0.1", CR_BACKUP_IDS, crData());
  const dev = new FakeFM1(image, { identity: "FM-1_930", bk: () => cf });
  const p = runPage({ navigator: midiOf(dev) });
  await p.ready;
  ok(!p.$["backup-go"].disabled && p.$["restore-go"].disabled, "Back up enabled, Restore waits for a file");
  let blobText = null;
  const realCreate = URL.createObjectURL;
  URL.createObjectURL = (b) => { b.text().then((x) => { blobText = x; }); return "blob:test"; };
  await p.$["backup-go"].fire("click");
  await until(() => blobText);
  URL.createObjectURL = realCreate;
  const file = blobText && JSON.parse(blobText);
  ok(p.downloads.length === 1 && /^coralflower-backup-/.test(p.downloads[0].name) && p.$["backup-status"].textContent.startsWith(en.backupSaved) && file && file.firmware === "CoralFlower 0.1" &&
     file.objects.length === CR_BACKUP_IDS.length, "Back up: coralflower-backup-*.json with CoralFlower's objects");
  const blank = backupSide("CoralFlower 0.1", CR_BACKUP_IDS);
  dev.bk = () => blank;
  p.$["restore-file"].files = [{ text: async () => "{\"format\":\"other\"}" }];
  await p.$["restore-file"].fire("change");
  ok(p.$["backup-status"].textContent.startsWith(en.badBackup) && p.$["restore-go"].disabled && p.$.status.textContent === "", "Restore: a file that is not a backup is refused, said in its own tool");
  p.$["restore-file"].files = [{ text: async () => blobText }];
  await p.$["restore-file"].fire("change");
  await p.$["restore-go"].fire("click");
  ok(p.confirms.at(-1) === en.restoreConfirm && blank.log.at(-1) === 1 && crData().every(([id, v]) => blank.objs.get(id)?.every((b, i) => b === v[i])) &&
     blank.restarts === 1 && p.$["backup-status"].textContent.startsWith(en.restored), "Restore: confirmed, every object back, the settings last, RESTART (CoralFlower restarts)");
}
{
  // CoralFlower 0.3.1: the sequencer's patterns and song in the file and back; on 0.3 (no such objects) kept in the file
  const CF031 = CF_BACKUP_IDS.filter((id) => id < 56);   // (0.3.1 lists neither 0.4.2's sound edits nor 0.5's pattern sounds)
  const cf = backupSide("CoralFlower 0.3.1", CF031, cfData());
  const dev = new FakeFM1(image, { identity: "FM-1_930", bk: () => cf });
  const p = runPage({ navigator: midiOf(dev) });
  await p.ready;
  let blobText = null;
  const realCreate = URL.createObjectURL;
  URL.createObjectURL = (b) => { b.text().then((x) => { blobText = x; }); return "blob:test"; };
  await p.$["backup-go"].fire("click");
  await until(() => blobText);
  URL.createObjectURL = realCreate;
  const file = blobText && JSON.parse(blobText);
  ok(file && file.objects.length === CF031.length && [50, 51, 52, 53, 54, 55].every((id) => file.objects.find((o) => o.id === id)?.size) &&
     readBackup(blobText).objects.length === CF031.length, "Back up on 0.3.1: the sequencer's six parts in the file (3840 B each at most: accepted)");
  const blank = backupSide("CoralFlower 0.3.1", CF031);
  dev.bk = () => blank;
  p.$["restore-file"].files = [{ text: async () => blobText }];
  await p.$["restore-file"].fire("change");
  await p.$["restore-go"].fire("click");
  ok(cfData().every(([id, v]) => blank.objs.get(id)?.every((b, i) => b === v[i])) && blank.restarts === 1 &&
     p.$["backup-status"].textContent.startsWith(en.restored) && p.$["backup-status"].textContent.endsWith("CoralFlower loop, CoralFlower patterns and song"),
     "Restore on 0.3.1: the patterns and song back, named once");
  const old = backupSide("CoralFlower 0.3", CF_BACKUP_IDS.filter((id) => id < 50));
  dev.bk = () => old;
  await p.$["restore-go"].fire("click");
  ok(!old.log.some((id) => id >= 50) && old.objs.get(30) && p.$["backup-status"].textContent.endsWith(en.restoreSkipped + "CoralFlower patterns and song)"),
     "Restore on 0.3: the rest restored, the patterns kept in the file and named once as not used");
  ok(objectNames([50, 6, 51, 55]) === "CoralFlower patterns and song, user sounds 1-16", "objectNames: each name once, in order");
  ok(objectNames([57, 30, 56, 58]) === "CoralFlower unsaved sound edits, CoralFlower settings", "objectNames: the three sounds' edits named once (0.4.2)");
}

// the return to the official V15 (validateStockPackage stands in for the real file, which is not in the repo)
{
  const stockImage = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const prelude = "validateStockPackage = async (b) => ({ product: \"FM-1_015\", image: __stockImage, sha256: \"test\" });";
  const v15 = { size: STOCK_V15_SIZE, arrayBuffer: async () => new ArrayBuffer(STOCK_V15_SIZE) };
  const cfSide = () => backupSide("CoralFlower 0.1", CR_BACKUP_IDS, crData());
  const run = async (answer) => {
    const dev = new FakeFM1(stockImage, { identity: "FM-1_930", final: "FM-1_015", loader: "ota-FM-1_015", bk: (id) => (id === "FM-1_930" ? cfSide() : null) });
    const p = runPage({ navigator: midiOf(dev), prelude, extra: { __stockImage: stockImage }, confirm: () => answer });
    await p.ready;
    p.$["stock-file"].files = [{ size: 123, arrayBuffer: async () => new ArrayBuffer(123) }];
    await p.$["stock-file"].fire("change");
    const wrong = p.$["stock-status"].textContent === en.stockError + en.stockWrongFile && p.$["stock-go"].disabled && p.$.stock.open === true;
    p.$["stock-file"].files = [v15];
    await p.$["stock-file"].fire("change");
    const valid = p.$["stock-status"].textContent === en.stockValid && !p.$["stock-go"].disabled;
    await p.$["stock-go"].fire("click");
    return { p, dev, wrong, valid };
  };
  let r = await run(false);
  ok(r.wrong && r.valid, "stock: a file of the wrong size refused, the V15 (stand-in) accepted, in its own section");
  ok(r.p.downloads.length === 1 && /^coralflower-backup-/.test(r.p.downloads[0].name) && r.p.confirms.length === 1 && r.p.confirms[0] === en.stockConfirm &&
     r.dev.upgrades === 0 && r.p.$["stock-status"].textContent === en.cancelled, "stock: the backup saved first; declined -> nothing written");
  r = await run(true);
  ok(r.p.$["stock-status"].textContent === en.done && r.dev.identity === "FM-1_015" && r.dev.bad === 0 && r.p.$["stock-done"].hidden === false &&
     r.p.$["stock-done-data"].textContent === en.stockDnSaved.replace("{file}", r.p.downloads[0].name) && r.p.$.status.textContent === "" && r.p.$.bar.value === 0,
     "stock: confirmed -> backup, FM-1_015, the done panel names the backup; the install card left idle");
}

/* ------------------------------ (f) the real package builder, when the SDK files are here --- */
{
  const sdk = join(ROOT, "tools/sdk");
  if (!existsSync(join(sdk, "cpu/wl82/tools/uboot.boot"))) console.log("tools/fm1pkg_make.py package: skipped (no tools/sdk)");
  else {
    const dir = mkdtempSync(join(tmpdir(), "cf-installer-"));
    try {
      writeFileSync(join(dir, "app.bin"), fillB(0x9000, 21));
      writeFileSync(join(dir, "ota.bin"), concat(fillB(0x900, 22), ascii("FELUCCA-LOADER-1"), fillB(0x200, 23)));
      const r = spawnSync("python3", [join(ROOT, "tools/fm1pkg_make.py"), join(dir, "app.bin"), join(dir, "ota.bin"), join(dir, "cf.fwsc"), "--product", "FM-1_930", "--sdk", sdk], { encoding: "utf8" });
      if (r.status !== 0) console.log(`tools/fm1pkg_make.py package: skipped (${(r.stderr || (r.error && r.error.message) || "").trim().split("\n").pop()})`);
      else {
        const real = new Uint8Array(readFileSync(join(dir, "cf.fwsc")));
        const p = runPage({ navigator: midiOf(new FakeFM1(logicalImage(real))) });
        await p.ready;
        await choose(p, "cf.fwsc", real);
        ok(p.$.pkg.className === "pkg ok" && !p.$.go.disabled, `tools/fm1pkg_make.py package (the real builder, tools/sdk): ${real.length} B, accepted by the page`);
      }
    } finally { rmSync(dir, { recursive: true, force: true }); }
  }
}

console.log(failed ? `\n${failed} FAILED` : "\nall passed");
process.exit(failed ? 1 : 0);
