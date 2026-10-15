# CoralFlower site: final QA (2026-10-15)

The four passes of the site-factory method (lp-qa), on `web/index.html` (`/`), `tools/emu/web/index.html` (`/play/`)
and `web/install/index.html` (`/install/`), as `tools/make_site.sh` puts them together. Run level: a headless Chrome
over the DevTools protocol, the in-app browser for the clicks, and static checks (an HTML parser, `node --check`).

```
FINAL QA
Pass 1 (design system): every font-size, font-weight, font-family, colour, padding, margin, gap, border-radius,
  @media and duration in site.css, and every style= in the pages, against DESIGN.md's whitelists.
  Fixed on the way: 3 raw line-heights, 2 letter-spacings, a 2 px gap twice, kbd's 3px 6px padding and 4px radius,
  a 50% radius, a 0.15em margin, the header's raw rgb(), the 1 ms reduced-motion durations (now --dur-none), the 56rem
  breakpoint (merged into 60rem), an unused utility class. Kept by rule: @font-face weights 400 and 600 (the two
  --fw tokens). Pages: no <style>, no colour in the markup, style= only sets a token. -> violations: 0
Pass 2 (UX and accessibility): contrast measured on every text node in its real scope, at 1440, 375 and 360 px, with
  every fold open and every message panel shown: 0 below 4.5:1 (3:1 large). Every scope sets its own color. Targets:
  0 under 24 px; buttons and footer links 44 px tall. Focus ring 3 px in each scope's focus colour; a skip link; the
  header's Install is invisible and unfocusable while the first screen's button is in view. Keyboard: Tab moves through
  the page; on /play/ Tab picks a knob only while the panel has focus, Esc leaves it, Enter and Space press a focused
  button (before: the emulator took Tab, Enter and Space everywhere, a keyboard trap). Every img has alt and a size;
  one h1 per page; prefers-reduced-motion honoured; dialog height in dvh, no 100vh. UX laws: Hick (four section
  links and Play), Miller (the first screen: the name, a line, one button with its version, one link), Fitts (the
  installer's button 64 px), von Restorff (one primary button per screen), peak-end (the rose last section).
  Fixed on the way: a fold's long SHA-256 widened the phone page to 475 px; checkboxes 20 px; "Show the log" 23 px
  tall. -> violations: 0
Pass 3 (performance and technique): scrollWidth equals clientWidth at 1440, 375 and 360 on both pages. Requests to
  other sites: 0. Fonts: 2 families, self-hosted WOFF, font-display swap (46 KB). The hero GIF has fetchpriority high;
  other images lazy. First page, compressed: HTML 22 KB, CSS 8 KB, modules 14 KB, fonts 46 KB, GIF 233 KB. Without Web
  MIDI (phones, Safari, Firefox) the 600 KB package is no longer downloaded. HTML balanced, no duplicate id, every
  inline script passes node --check. Title, description, Open Graph and icons on both pages. Without JavaScript the
  content shows, and a note says the installer needs it. Screenshots at 375 and 1440 looked at.
  Tests: node web/test_installer.mjs, 105 checks, all pass. -> violations: 0
Pass 4 (content and law): English copy reread as a native reader: verbless taglines rewritten into sentences
  ("Installing takes about a minute."), curly apostrophes and quotes in the page text. Claims match the README
  (26 scales, nine arpeggiator modes, about 60 sounds). No reviews, timers or counters, so no cookie notice. No form
  collects personal data. Trademarks: M-VAVE, FM-1 and Orchid named with the no-affiliation line. Donations described
  as a voluntary gift, nothing sold. Kept as is: the installer libraries' own device names (shared with the Python
  installer) keep straight apostrophes. -> violations: 0
VERDICT: ready to ship
```

## Not a violation

- The audit's element scan lists `.device` on phones: its rose glow (`::before`) reaches past the screen and is
  clipped by its section (`overflow: clip`). The page does not scroll sideways (scrollWidth equals clientWidth).
