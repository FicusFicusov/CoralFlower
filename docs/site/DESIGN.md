# CoralFlower site: design system

The tokens live in `web/site.css`, shared by both pages: the first page `web/index.html` (`/`) and the emulator page
`tools/emu/web/index.html` (`/play/`). A page uses tokens only. It has no `<style>` block, no colour in its markup, and
a `style=` attribute only sets a token (for example `--fact: var(--c-rose-500)`). `web/test_installer.mjs` checks this.

## Layers

1. **Primitives:** raw colours, named by hue and step (`--c-night-900`, `--c-rose-500`, …). Components never use them,
   with three exceptions: the motif (the ramp and the facts' squares), the pixel icons (`.px-*`) and `::selection`.
2. **Semantics:** what a colour is for (`--color-bg`, `--color-text`, `--color-accent`, …). Each scope declares them
   again and sets `color: var(--color-text)` and `background: var(--color-bg)` itself.
3. **Type, space, shape, motion:** the scales below.

## Scopes and contrast (WCAG 2.2, computed)

| Scope | Where | Text on bg | Muted on bg / surface / raised | Accent | Link | Focus |
|---|---|---|---|---|---|---|
| dark (`:root`) | hero, facts, install, FAQ, footer | 16.68 | 7.16 / 6.73 / 6.11 | rose 6.26, on-accent 6.26 | sea 12.73 | fire 12.30 |
| stage (`.scope-stage`) | garden, play | 17.03 | 7.31 | as dark | as dark | as dark |
| surface (`.scope-surface`) | safety and tools | 15.69 | 6.73 / 6.11 / 4.94 | 5.89 | 11.98 | 11.57 |
| inverse (`.scope-inverse`) | features | 15.69 | 6.56 / 5.79 | rose-800 5.49, cream on it 5.49 | sea-800 5.17 | rose-800 5.49 |
| accent (`.scope-accent`) | the last section | 6.26 | rose-950 5.05 | night, cream on it 16.68 | night 6.26 | night 6.26 |

Decorative only, never text: `--color-border` (1.45 dark, 2.02 inverse). Controls use `--color-border-strong`
(3.87 dark, 6.56 inverse).

## Whitelists

- **font-family:** `var(--font-display)` (Departure Mono: names, headings, buttons, labels), `var(--font-text)`
  (Inter Tight: text), `var(--font-mono)` (logs, addresses). Two families, both from this site, `font-display: swap`.
- **font-size:** `--fs-xs` 13–14, `--fs-sm` 15–16, `--fs-base` 17–18, `--fs-lg` 19–22, `--fs-xl` 22–33, `--fs-2xl`
  33–55, `--fs-3xl` 44–72 px. Fluid from a 375 px to a 1440 px window.
- **font-weight:** `--fw-regular` 400, `--fw-strong` 600. (`@font-face` names the same two.)
- **line-height:** `--lh-display` 1, `--lh-heading` 1.1, `--lh-snug` 1.25, `--lh-body` 1.55.
- **letter-spacing:** `--ls-display` −0.01em, `--ls-button` 0.02em, `--ls-caps` 0.08em.
- **padding, margin, gap:** `--space-4xs` 2, `--space-3xs` 4, `--space-2xs` 8, `--space-xs` 12, `--space-s` 16,
  `--space-m` 24, `--space-l` 32, `--space-xl` 48, `--space-2xl` 64, `--space-3xl` 96 px; `--space-gutter` 16–40,
  `--space-section` 64–128, `--space-band` 24–40 px; `0`, `auto`, and `calc(-1 * <token>)`.
- **border-radius:** `--radius-s` 6, `--radius-m` 10, `--radius-l` 18 px, `--radius-pill`.
- **@media:** `min-width: 40rem`, `60rem`, `72rem`; `max-width: 39.99rem` (the phone side of 40rem);
  `prefers-reduced-motion: reduce`.
- **durations:** `--dur-fast` 120, `--dur-base` 200, `--dur-slow` 420 ms; `--dur-none` 1 ms under reduced motion.
  Easing: `--ease-out`.
- **shadows:** `--shadow-s`, `--shadow-m`, `--shadow-accent`, `--shadow-paper`. Tinted with the night or the rose,
  never pure black.
- **z-index:** `--z-header` 10, `--z-skip` 20.

## Components

| Component | Variants | Where |
|---|---|---|
| button `.btn` | `--primary`, `--secondary`, `--ghost`, `--support`; `--sm`, default, `--lg`; `--block` | everywhere |
| card `.card` | `--feature` (a screen, a title, a line), `--icon` (a pixel icon beside them) | features, steps, safety, legend |
| section `.section` | default, `--band`; scopes stage, surface, inverse, accent | every page |
| section head | eyebrow (the ramp's nine dots and a word), `display-2`, `lead`; `--center` | every section |
| fold `.fold` | default, `--quiet` | the tools, the FAQ, the checklist |
| note, panel | `note--warn`, `--bad`, `--ok`; `panel--ok`, `--bad` | the installer's messages |
| device | a screen in a bezel on a glow | the hero, the emulator |
| pixel icon | 48 px, `--xl` 96 px; own glyphs on a 12 × 12 grid | steps, safety, the last section |

## Motif

The plant's nine colours as pixels: the strip under the header, the nine dots before every eyebrow, the facts'
squares, the pixel icons and the flower. It shows in every section.
