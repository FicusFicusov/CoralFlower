# CoralFlower site: structure

Made with the site-factory method (lp-structure): every section answers one question or objection, one CTA verb.

```
СТРУКТУРА
Тип CTA: установка (firmware onto a device, from the browser) / CTA-глагол: «Install» («Install on your FM-1»)
Навигация: 4 якоря [Features · Garden · Safety · FAQ] + «Play» (the emulator page) + the CTA button «Install»
Секции по порядку:
1. Hero — what it is, for which device, one action. Objection: «Is this for my FM-1? What is it?»
   → name, one line, the Install button (the real installer: one click), «Play it in your browser» (secondary),
     the flower growing on its screen. Dark, rich; the peak.
2. Proof strip — «Free and open source · About a minute to install · Nothing to download · Your sounds backed up first». Objection: «What does it cost me?»
   → four facts in a row. Dark, compact.
3. Features — what you get, in four modules with screens. Objection: «Is it worth replacing my firmware?»
   → Chords from two keys · Arpeggiator and strum · Looper · Sounds that breathe. INVERSE (cream), cards.
4. Garden — the plant and the coral that grow from the music. Objection: «Why this one and not another chord
   firmware?» → a section built around the visual (the GIF, plant and coral side by side). Stage (black), full bleed.
5. Install — three steps next to the button. Objection: «Is it hard? Do I need tools or drivers?»
   → USB · Chrome or Edge · Press Install (≈1 min); the installer itself (button, progress, done / stopped).
   Dark surface, container.
6. Safety — «You can always go back». Objection: «Can I brick it? Will I lose my sounds?»
   → automatic backup · back to the official firmware any time · open source; the tools fold here (back up and
   restore, device not found, return to the official firmware, what is on my FM-1). Dark, cards.
7. FAQ — 6 questions: browsers and systems, which firmwares it installs over, sounds kept, updates, price, an
   install that stopped. Dark, narrow typographic column.
8. Final CTA — «Grow your first chord» + Install + Play. Objection: the doubt left at the end. ACCENT (rose), full
   bleed; the end.
Footer: credits (ChoralRoot, Felucca, Melodee, fonts), licence, source, contact, Support (wallets dialog).
Ритм: dark-rich (1) → dark-compact (2) → INVERSE cream cards (3) → black stage full-bleed visual (4) →
      dark surface module (5) → dark cards (6) → dark typographic narrow (7) → ACCENT rose full-bleed (8) → footer
Модульность:
  button → primary lg (hero, install, final), primary md (header), secondary (Play, file pick, tools), ghost (links)
  card → feature (inverse, with a screen), safety (dark, with a pixel icon), step (dark, numbered)
  section → default / inverse / stage / accent; section head (eyebrow h2 + display line)
  fold (details) → tools and FAQ items
  pixel icon → steps and safety (own SVG glyphs, not an icon set)
  motif → the 9-colour pixel ramp (header, section heads, final CTA) and the pixel flower (favicon, steps, hero)
Форма: none (the action is the install button); the installer's choices fold into Safety.
Emulator: its own page (/play/), the same header and tokens; the landing links to it as the secondary action.
```

## Gate

- Every section names the objection it answers; none is there "just because".
- The three main objections (risk, effort, worth) each have a section: Safety (6), Install (5), Features and
  Garden (3, 4).
- The rhythm has an inverse section (3) and an accent section (8), and alternates.
- One CTA verb, «Install», repeated: the header, the hero, the install section, the final CTA.
