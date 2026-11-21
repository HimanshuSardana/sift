// Builds compositions/*.html from the real sift screen captures.
// Every frame on screen is a capture of the running binary; this script only
// positions it on the 108 x 29 grid and writes the camera timeline.
import fs from "node:fs";
import path from "node:path";
import { parseAnsi, textRuns } from "./ansi.mjs";

const ROOT = path.resolve(import.meta.dirname, "..");
const COLS = 108;
const ROWS = 29;
const W = 1920;
const H = 1080;
const ADV = W / COLS; // 17.7778
const LINE = H / ROWS; // 37.2414
const FONT = ADV / 0.6; // JetBrains Mono advance = 0.6 em

const BOX = {
  "─": "h", "│": "v", "┌": "rd", "┐": "ld", "└": "ru", "┘": "lu",
  "├": "rv", "┤": "lv", "┬": "hd", "┴": "hu", "┼": "hv",
};

const FG = {
  ink: ["#D2D8DE", "#EDF1F5"],
  cyan: ["#56C4D6", "#7FD3E2"],
  magenta: ["#C48BE0", "#DCA6F4"],
  yellow: ["#CBB26A", "#E6D296"],
  blue: ["#7FA6E0", "#A2C2F7"],
  green: ["#8CC08A", "#AEDCAC"],
  red: ["#D86C74", "#F09299"],
  black: ["#0C0F12", "#0C0F12"],
};

const cache = new Map();
function load(name) {
  if (!cache.has(name)) {
    const src = fs.readFileSync(path.join(ROOT, "captures", `${name}.ansi`), "utf8");
    const grid = parseAnsi(src, COLS, ROWS);
    cache.set(name, { grid, runs: textRuns(grid) });
  }
  return cache.get(name);
}

const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

// One box glyph -> one or two 1.5px strokes.
function glyphStrokes(ch, x, y, out) {
  const x0 = x * ADV, x1 = x0 + ADV, cx = x0 + ADV / 2;
  const y0 = y * LINE, y1 = y0 + LINE, cy = y0 + LINE / 2;
  const h = (a, b) => out.push([a, cy, b - a, 1.5]);
  const v = (a, b) => out.push([cx, a, 1.5, b - a]);
  switch (BOX[ch]) {
    case "h": h(x0, x1); break;
    case "v": v(y0, y1); break;
    case "rd": h(cx, x1); v(cy, y1); break;
    case "ld": h(x0, cx); v(cy, y1); break;
    case "ru": h(cx, x1); v(y0, cy); break;
    case "lu": h(x0, cx); v(y0, cy); break;
    case "rv": h(cx, x1); v(y0, y1); break;
    case "lv": h(x0, cx); v(y0, y1); break;
    case "hd": h(x0, x1); v(cy, y1); break;
    case "hu": h(x0, x1); v(y0, cy); break;
    case "hv": h(x0, x1); v(y0, y1); break;
  }
}

function screen(name, hide = []) {
  const { grid, runs } = load(name);
  const hidden = (r) => hide.some((h) => r.y === h.row && r.x + r.text.length > h.x0 && r.x < h.x1);
  const lineRows = new Set();
  grid.forEach((row, y) => {
    for (const c of row) if (c.t === "─") lineRows.add(y);
  });
  const parts = [];
  grid.forEach((row, y) => {
    let x = 0;
    while (x < COLS) {
      if (row[x].t === "─") {
        let e = x;
        while (e + 1 < COLS && row[e + 1].t === "─") e += 1;
        const op = row[x].b ? 1 : row[x].d ? 0.45 : 1;
        parts.push(`<i class="bl" style="left:${x * ADV}px;top:${y * LINE + LINE / 2 - 0.75}px;width:${(e - x + 1) * ADV}px;height:1.5px;opacity:${op}"></i>`);
        x = e + 1;
        continue;
      }
      if (BOX[row[x].t]) {
        const c = row[x];
        const op = c.b ? 1 : c.d ? 0.45 : 1;
        const out = [];
        glyphStrokes(c.t, x, y, out);
        for (const [px, py, w, h] of out)
          parts.push(`<i class="bl" style="left:${px}px;top:${py}px;width:${w}px;height:${h}px;opacity:${op}"></i>`);
      }
      x += 1;
    }
  });
  const spans = [];
  for (const r of runs) {
    if (hidden(r)) continue;
    const base = FG[r.fg] ?? FG.ink;
    const color = r.b ? base[1] : base[0];
    let style = `left:${(r.x * ADV).toFixed(3)}px;top:${(r.y * LINE).toFixed(3)}px;color:${color};font-weight:${r.b ? 500 : 400};`;
    if (r.d) style += "opacity:0.62;";
    if (r.u) style += "text-decoration:underline;text-underline-offset:3px;";
    if (r.r) {
      style += r.d
        ? "background:#39434C;color:#0C0F12;font-weight:400;opacity:1;"
        : `background:${color};color:#0C0F12;opacity:1;`;
    }
    if (lineRows.has(r.y) && !r.r) style += "background:var(--ground);";
    spans.push(`<span class="r" style="${style}">${esc(r.text)}</span>`);
  }
  return parts.join("") + spans.join("");
}

const CSS = `
#root { position: absolute; inset: 0; overflow: hidden; background: var(--ground);
  font-family: "JetBrains Mono", ui-monospace, monospace;
  font-variant-ligatures: none; -webkit-font-smoothing: antialiased; }
.stage { position: absolute; left: 96px; top: 24px; width: 1728px; height: 972px; overflow: hidden; }
.fit { position: absolute; left: 0; top: 0; width: 1920px; height: 1080px; transform: scale(0.9); transform-origin: 0 0; }
.cam { position: absolute; inset: 0; }
.cap { position: absolute; left: 96px; top: 1026px; width: 1728px; font-size: 27px; line-height: 1.3;
  color: #8A939D; white-space: pre; }
.cap b { color: var(--cyan); font-weight: 400; }
.state { position: absolute; inset: 0; }
.r { position: absolute; white-space: pre; font-size: ${FONT.toFixed(4)}px;
  line-height: ${LINE.toFixed(4)}px; height: ${LINE.toFixed(4)}px; letter-spacing: 0; }
.bl { position: absolute; background: var(--cyan); }
.card { position: absolute; left: 120px; width: 1680px; }
.dim { color: var(--dim); }
.ink { color: var(--ink); }
.cyan { color: var(--cyan); }
`;

// ---- scene spec -----------------------------------------------------------

const scenes = [];
const add = (s) => scenes.push(s);
const px = (col, row) => [col * ADV, row * LINE];
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
// Scale about the frame centre, then translate towards the subject. The
// translate is clamped so the screen still covers the frame: |tx| <= 960*(s-1).
// Align a content column to the frame's left edge (with a small gutter), so a
// zoomed reading shot never cuts a line mid-word on its left.
function panLeft(scale, col, gutter = 0, row = 14) {
  const x = col * ADV;
  const [, fy] = px(0, row);
  return {
    scale,
    tx: clamp(gutter - W / 2 - scale * (x - W / 2), -W / 2 * (scale - 1), W / 2 * (scale - 1)),
    ty: clamp(-scale * (fy - H / 2), -H / 2 * (scale - 1), H / 2 * (scale - 1)),
  };
}

function pan(scale, focus) {
  const [fx, fy] = px(focus[0], focus[1]);
  return {
    scale,
    tx: clamp(-scale * (fx - W / 2), -W / 2 * (scale - 1), W / 2 * (scale - 1)),
    ty: clamp(-scale * (fy - H / 2), -H / 2 * (scale - 1), H / 2 * (scale - 1)),
  };
}

function ui({ id, start, dur, states, cam = [], typed = [], extra = "", cap }) {
  return { kind: "ui", id, start, dur, states, cam, typed, extra, cap };
}

const c = (col, row) => px(col, row);

add(
  ui({
    id: "s1-hook",
    start: 0,
    dur: 2.6,
    states: [{ cap: "c2_scraped", at: 0 }],
    cam: [
      { at: 0, ...panLeft(2.62, 22, 34, 13.5) },
      { at: 1.55, ...pan(1, [54, 14.5]), dur: 0.62, ease: "power3.inOut" },
    ],
  }),
);

add(
  ui({
    id: "s2-feeds",
    start: 2.6,
    dur: 1.35,
    cap: "<b>r</b> refetches · three feeds, one SQLite file",
    states: [
      { cap: "a1_dash", at: 0 },
      { cap: "a2_lwn", at: 0.34 },
      { cap: "a3_rust", at: 0.62 },
      { cap: "a4_fetched", at: 0.92 },
    ],
    cam: [{ at: 0, ...pan(1, [54, 14]) }],
  }),
);

add({ kind: "rss", id: "s3-rss", start: 3.95, dur: 0.85 });

add(
  ui({
    id: "s4-posts",
    start: 4.8,
    dur: 2.0,
    cap: "<b>l</b> opens the feed · <b>Ctrl-→</b> widens the list",
    states: [
      { cap: "b1_posts", at: 0 },
      { cap: "b4_posts_resized", at: 0.62 },
      { cap: "c1_stub", at: 1.28 },
    ],
    cam: [{ at: 0, ...pan(1, [54, 14]) }],
  }),
);

add(
  ui({
    id: "s5-scrape",
    start: 6.8,
    dur: 2.2,
    cap: "<b>s</b> scrapes the page and keeps the prose",
    states: [
      { cap: "c1_stub", at: 0 },
      { cap: "c2_scraped", at: 0.75 },
    ],
    cam: [{ at: 0, ...pan(1, [54, 14]) }],
  }),
);

add(
  ui({
    id: "s6-text",
    start: 9.0,
    dur: 1.9,
    cap: "Bold headings, dim meta, underlined links",
    states: [
      { cap: "g0", at: 0 },
      { cap: "g2", at: 1.15 },
      { cap: "g4", at: 1.35 },
      { cap: "g6", at: 1.55 },
    ],
    cam: [
      { at: 0, ...pan(1, [54, 14]) },
      { at: 0.45, ...panLeft(1.24, 22, 20, 13), dur: 0.66, ease: "power3.inOut" },
    ],
  }),
);

add(
  ui({
    id: "s7-search",
    start: 10.9,
    dur: 1.7,
    cap: "<b>/</b> finds · <b>n</b> and <b>N</b> step through matches",
    states: [
      { cap: "d1_search_typing", at: 0 },
      { cap: "d2_search_hit", at: 0.82 },
    ],
    typed: [{ at: 0.08, text: "breach", cps: 16, x: 0, y: 28, label: "Search: ", hideAfter: 0.82 }],
    cam: [{ at: 0, ...pan(1, [54, 14]) }],
  }),
);

add(
  ui({
    id: "s8-focus",
    start: 12.6,
    dur: 1.6,
    cap: "<b>Ctrl-F</b> · 80 columns, centred, no chrome",
    states: [{ cap: "i2_focus", at: 0 }],
    cam: [
      { at: 0, ...pan(1.28, [54, 12]) },
      { at: 0.55, ...pan(1.0, [54, 14]), dur: 0.66, ease: "power3.inOut" },
    ],
  }),
);

add(
  ui({
    id: "s9-links",
    start: 14.2,
    dur: 1.8,
    cap: "<b>f</b> · every link in the article, searchable",
    states: [
      { cap: "l1_links11", at: 0 },
      { cap: "l2_links_pull", at: 1.0 },
    ],
    typed: [{ at: 0.5, text: "pull", cps: 14, x: 17, y: 5, label: "> " }],
    cam: [
      { at: 0, ...pan(1.04, [49, 12]) },
      { at: 0.3, ...pan(1.3, [49, 12]), dur: 0.55, ease: "power3.inOut" },
    ],
  }),
);

add({ kind: "url", id: "s10-url", start: 16.0, dur: 1.5 });

add(
  ui({
    id: "s10b-page",
    start: 17.5,
    dur: 1.2,
    cap: "<b>sift &lt;url&gt;</b> reads one page, no database",
    states: [{ cap: "u1_wiki", at: 0 }],
    cam: [
      { at: 0, ...pan(1.0, [54, 14]) },
      { at: 0.2, ...pan(1.06, [54, 13]), dur: 0.7, ease: "power3.out" },
    ],
  }),
);

add(
  ui({
    id: "s11-images",
    start: 18.7,
    dur: 2.4,
    cap: "<b>i</b> · images fetched and drawn in the terminal",
    states: [
      { cap: "u1_wiki", at: 0 },
      { cap: "u2_wiki_palette", at: 0.26 },
    ],
    cam: [
      { at: 0, ...pan(1.02, [54, 13]) },
      { at: 0.42, ...pan(1.62, [72, 13]), dur: 0.66, ease: "power3.inOut" },
    ],
    extra: `<img id="s11-photo" class="photo" src="assets/apollo11.jpg" alt="" />`,
  }),
);

add({ kind: "end", id: "s12-end", start: 21.1, dur: 3.5 });

// ---- emit -----------------------------------------------------------------

function emitUi(s) {
  const hide = s.typed.map((t) => ({ row: t.y, x0: t.x, x1: t.x + (t.label?.length ?? 0) + t.text.length + 1 }));
  const states = s.states
    .map(
      (st, i) =>
        `<div id="${s.id}-st${i}" class="state" style="opacity:${i === 0 ? 1 : 0}">${screen(st.cap, hide)}</div>`,
    )
    .join("\n");

  const typed = s.typed
    .map((t) => {
      const chars = [...t.text]
        .map((ch, i) => `<span id="${s.id}-t${i}" style="opacity:0">${esc(ch === " " ? "\u00a0" : ch)}</span>`)
        .join("");
      const label = t.label ? `<span style="color:var(--ink)">${esc(t.label)}</span>` : "";
      return `<div id="${s.id}-type" class="r typed" style="left:${(t.x * ADV).toFixed(3)}px;top:${(t.y * LINE).toFixed(3)}px;color:var(--ink)">${label}${chars}</div>`;
    })
    .join("");

  const cap = s.cap ? `<div id="${s.id}-cap" class="cap">${s.cap}</div>` : "";

  const tl = [`const tl = gsap.timeline({ paused: true });`];
  if (s.cap)
    tl.push(
      `tl.fromTo("#${s.id}-cap", { opacity: 0, y: 6 }, { opacity: 1, y: 0, duration: 0.28, ease: "power3.out" }, 0.12);`,
    );
  const cam = [`#${s.id}-cam`];
  const first = s.cam[0];
  tl.push(
    `gsap.set("${cam[0]}", { scale: ${first.scale}, x: ${first.tx.toFixed(2)}, y: ${first.ty.toFixed(2)} });`,
  );
  for (const k of s.cam.slice(1)) {
    tl.push(
      `tl.to("${cam[0]}", { scale: ${k.scale}, x: ${(k.tx ?? 0).toFixed(2)}, y: ${(k.ty ?? 0).toFixed(2)}, duration: ${k.dur ?? 0.5}, ease: "${k.ease ?? "power3.inOut"}" }, ${k.at});`,
    );
  }
  s.states.slice(1).forEach((st, i) => {
    tl.push(`tl.set("#${s.id}-st${i}", { opacity: 0 }, ${st.at});`);
    tl.push(`tl.set("#${s.id}-st${i + 1}", { opacity: 1 }, ${st.at});`);
  });
  for (const t of s.typed) {
    [...t.text].forEach((ch, i) => {
      if (ch === " ") return;
      tl.push(`tl.set("#${s.id}-t${i}", { opacity: 1 }, ${(t.at + i / t.cps).toFixed(3)});`);
    });
    if (t.hideAfter !== undefined) tl.push(`tl.set("#${s.id}-type", { opacity: 0 }, ${t.hideAfter});`);
  }
  tl.push(`window.__timelines["${s.id}"] = tl;`);

  const photoCss = s.extra
    ? `#s11-photo { position: absolute; left: ${(40 * ADV).toFixed(2)}px; top: ${(5 * LINE).toFixed(2)}px;
    width: ${(59 * ADV).toFixed(2)}px; height: ${(17 * LINE).toFixed(2)}px; object-fit: contain; object-position: center; }`
    : "";

  return `<template>
<style>${CSS}${photoCss}
#${s.id}-shot { position: absolute; inset: 0; }
</style>
<div id="root" data-composition-id="${s.id}" data-width="${W}" data-height="${H}">
  <div id="${s.id}-shot" class="clip" data-start="0" data-duration="${s.dur}" data-track-index="0">
    <div class="stage"><div class="fit">
      <div id="${s.id}-cam" class="cam">
${states}
${typed}
${s.extra}
      </div>
    </div></div>
${cap}
  </div>
  <script>
${tl.join("\n")}
  </script>
</div>
</template>
`;
}

function emitRss(s) {
  return `<template>
<style>${CSS}
#${s.id}-shot { position: absolute; inset: 0; }
#${s.id}-a { position: absolute; left: 96px; top: 448px; font-size: 92px; line-height: 1.05; color: var(--ink); letter-spacing: -0.01em; white-space: pre; clip-path: inset(0 100% 0 0); }
#${s.id}-b { position: absolute; left: 100px; top: 584px; font-size: 28px; color: var(--dim); white-space: pre; opacity: 0; }
</style>
<div id="root" data-composition-id="${s.id}" data-width="${W}" data-height="${H}">
  <div id="${s.id}-shot" class="clip" data-start="0" data-duration="${s.dur}" data-track-index="0">
    <div id="${s.id}-a">RSS 2.0 <span class="dim">·</span> RSS 1.0 <span class="dim">·</span> Atom</div>
    <div id="${s.id}-b">parsed by libxml2 into one local SQLite file</div>
  </div>
  <script>
    const tl = gsap.timeline({ paused: true });
    tl.fromTo("#${s.id}-a", { clipPath: "inset(0 100% 0 0)" }, { clipPath: "inset(0 0% 0 0)", duration: 0.22, ease: "power4.out" }, 0.06);
    tl.fromTo("#${s.id}-b", { opacity: 0 }, { opacity: 1, duration: 0.2, ease: "power2.out" }, 0.3);
    window.__timelines["${s.id}"] = tl;
  </script>
</div>
</template>
`;
}

function emitUrl(s) {
  const cmd = "sift en.wikipedia.org/wiki/Apollo_11";
  const pad = 2; // cells of indent before the prompt
  const sig = "~ $ ";
  const chars = [...cmd]
    .map((ch, i) => `<span id="${s.id}-c${i}" style="opacity:0">${esc(ch === " " ? "\u00a0" : ch)}</span>`)
    .join("");
  const tl = [`const tl = gsap.timeline({ paused: true });`];
  tl.push(`tl.fromTo("#${s.id}-prompt", { opacity: 0 }, { opacity: 1, duration: 0.16, ease: "power2.out" }, 0);`);
  [...cmd].forEach((ch, i) => {
    const t = (0.22 + i / 34).toFixed(3);
    if (ch !== " ") tl.push(`tl.set("#${s.id}-c${i}", { opacity: 1 }, ${t});`);
    tl.push(`tl.set("#${s.id}-cursor", { x: ${(i + 1) * ADV} }, ${t});`);
  });
  tl.push(`tl.set("#${s.id}-cursor", { opacity: 0 }, 1.22);`);
  tl.push(`window.__timelines["${s.id}"] = tl;`);
  return `<template>
<style>${CSS}
#${s.id}-shot { position: absolute; inset: 0; }
#${s.id}-prompt { position: absolute; left: ${(pad * ADV).toFixed(2)}px; top: ${(7 * LINE).toFixed(2)}px;
  font-size: ${FONT.toFixed(4)}px; line-height: ${LINE.toFixed(4)}px; white-space: pre; color: var(--ink); }
#${s.id}-prompt .sigil { color: var(--dim); }
#${s.id}-cursor { position: absolute; left: ${(sig.length * ADV).toFixed(2)}px; top: 2px;
  width: ${ADV.toFixed(3)}px; height: ${(LINE * 0.74).toFixed(2)}px; background: var(--ink); opacity: 0.92; }
</style>
<div id="root" data-composition-id="${s.id}" data-width="${W}" data-height="${H}">
  <div id="${s.id}-shot" class="clip" data-start="0" data-duration="${s.dur}" data-track-index="0">
    <div class="stage"><div class="fit">
      <div id="${s.id}-prompt"><span class="sigil">~</span> <span class="cyan">$</span> ${chars}<span id="${s.id}-cursor" data-layout-allow-occlusion></span></div>
    </div></div>
  </div>
  <script>
${tl.join("\n")}
  </script>
</div>
</template>
`;
}

function emitEnd(s) {
  const letters = [..."sift"]
    .map((ch, i) => `<span id="${s.id}-l${i}" class="ltr">${ch}</span>`)
    .join("");
  const cmd = "make && ./sift";
  const chars = [...cmd]
    .map((ch, i) => `<span id="${s.id}-c${i}" style="opacity:0">${esc(ch === " " ? "\u00a0" : ch)}</span>`)
    .join("");
  const tl = [`const tl = gsap.timeline({ paused: true });`];
  tl.push(`tl.fromTo("#${s.id}-l0, #${s.id}-l1, #${s.id}-l2, #${s.id}-l3",
    { opacity: 0.25, filter: "blur(5px)" },
    { opacity: 1, filter: "blur(0px)", duration: 0.2, ease: "power2.out", stagger: 0.1 }, 0.15);`);
  tl.push(`tl.fromTo("#${s.id}-claim", { clipPath: "inset(0 100% 0 0)" }, { clipPath: "inset(0 0% 0 0)", duration: 0.24, ease: "power4.out" }, 0.72);`);
  tl.push(`tl.fromTo("#${s.id}-note", { opacity: 0 }, { opacity: 1, duration: 0.22, ease: "power2.out" }, 1.0);`);
  tl.push(`tl.fromTo("#${s.id}-cmd", { opacity: 0 }, { opacity: 1, duration: 0.16, ease: "power2.out" }, 1.28);`);
  [...cmd].forEach((ch, i) => {
    const t = (1.34 + i / 26).toFixed(3);
    if (ch !== " ") tl.push(`tl.set("#${s.id}-c${i}", { opacity: 1 }, ${t});`);
    tl.push(`tl.set("#${s.id}-cursor", { x: ${(i + 1) * 20.4} }, ${t});`);
  });
  tl.push(`tl.set("#${s.id}-cursor", { opacity: 1 }, 2.0);`);
  tl.push(`tl.set("#${s.id}-cursor", { opacity: 0 }, 2.35);`);
  tl.push(`tl.set("#${s.id}-cursor", { opacity: 1 }, 2.7);`);
  tl.push(`window.__timelines["${s.id}"] = tl;`);
  return `<template>
<style>${CSS}
#${s.id}-shot { position: absolute; inset: 0; }
#${s.id}-mark { position: absolute; left: 96px; top: 296px; font-size: 240px; line-height: 1;
  letter-spacing: -0.045em; color: var(--ink); white-space: pre; }
#${s.id}-claim { position: absolute; left: 100px; top: 606px; font-size: 46px; line-height: 1.2;
  letter-spacing: -0.01em; color: var(--ink); white-space: pre; }
#${s.id}-note { position: absolute; left: 100px; top: 686px; font-size: 28px; color: var(--dim); white-space: pre; }
#${s.id}-cmd { position: absolute; left: 100px; top: 796px; font-size: 34px; white-space: pre; color: var(--ink); }
#${s.id}-cmd .sigil { color: var(--cyan); }
#${s.id}-cursor { position: absolute; left: 43px; top: 4px; width: 20px; height: 30px;
  background: var(--ink); opacity: 0; }
</style>
<div id="root" data-composition-id="${s.id}" data-width="${W}" data-height="${H}">
  <div id="${s.id}-shot" class="clip" data-start="0" data-duration="${s.dur}" data-track-index="0">
    <div id="${s.id}-mark">${letters}</div>
    <div id="${s.id}-claim">A terminal RSS reader and page reader</div>
    <div id="${s.id}-note">C++20 and ncurses only · no daemon · no browser · no JavaScript</div>
    <div id="${s.id}-cmd"><span class="sigil">$</span> ${chars}<span id="${s.id}-cursor" data-layout-allow-occlusion></span></div>
  </div>
  <script>
${tl.join("\n")}
  </script>
</div>
</template>
`;
}

const emitters = { ui: emitUi, rss: emitRss, url: emitUrl, end: emitEnd };

fs.mkdirSync(path.join(ROOT, "compositions"), { recursive: true });
for (const s of scenes) {
  const html = emitters[s.kind](s);
  fs.writeFileSync(path.join(ROOT, "compositions", `${s.id}.html`), html);
}

const sfx = [
  // [file, at, volume, fadeIn, fadeOut, duration, lane]
  ["key-press", 2.94, 0.75, 0, 0.06, 0.18, 9],
  ["key-press", 3.22, 0.75, 0, 0.06, 0.18, 9],
  ["click-soft", 3.52, 0.66, 0, 0.06, 0.18, 9],
  ["whoosh-short", 3.9, 0.39, 0.02, 0.14, 0.58, 10],
  ["key-press", 4.82, 0.75, 0, 0.06, 0.18, 9],
  ["click-soft", 5.42, 0.63, 0, 0.06, 0.18, 9],
  ["key-press", 6.08, 0.75, 0, 0.06, 0.18, 9],
  ["click-soft", 7.55, 0.69, 0, 0.08, 0.2, 9],
  ["key-press", 10.15, 0.51, 0, 0.06, 0.18, 9],
  ["key-press", 10.34, 0.51, 0, 0.06, 0.18, 9],
  ["key-press", 10.53, 0.51, 0, 0.06, 0.18, 9],
  ["typing", 11.02, 0.45, 0.02, 0.12, 0.5, 9],
  ["click-soft", 11.72, 0.69, 0, 0.06, 0.2, 9],
  ["key-press", 12.6, 0.69, 0, 0.06, 0.18, 9],
  ["key-press", 14.2, 0.69, 0, 0.06, 0.18, 9],
  ["click-soft", 14.5, 0.51, 0, 0.06, 0.18, 9],
  ["whoosh-short", 15.88, 0.39, 0.02, 0.14, 0.58, 10],
  ["typing", 16.2, 0.48, 0.02, 0.2, 1.0, 9],
  ["key-press", 17.3, 0.75, 0, 0.06, 0.18, 9],
  ["whoosh-short", 18.62, 0.36, 0.02, 0.14, 0.58, 10],
  ["key-press", 18.96, 0.69, 0, 0.06, 0.18, 9],
  ["impact-bass-1", 21.22, 0.51, 0.04, 0.9, 2.1, 9],
];

const audioTags = sfx
  .map(
    ([file, at, vol, fi, fo, dur, lane], i) =>
      `      <audio id="sfx-${String(i + 1).padStart(2, "0")}" class="clip" src="assets/sfx/${file}.mp3"
        data-start="${at}" data-duration="${dur}" data-track-index="${lane}" data-volume="${vol}"
        ${fi ? `data-fade-in="${fi}"` : ""} ${fo ? `data-fade-out="${fo}"` : ""}></audio>`,
  )
  .join("\n");

const hosts = scenes
  .map(
    (s) =>
      `      <div id="${s.id}" data-composition-id="${s.id}" data-composition-src="compositions/${s.id}.html"
        data-start="${s.start}" data-duration="${s.dur}" data-track-index="0" data-track-kind="graphics"
        data-width="${W}" data-height="${H}"></div>`,
  )
  .join("\n");

const total = scenes.at(-1).start + scenes.at(-1).dur;
const index = `<!doctype html>
<html lang="en"
  data-composition-variables='[
    {"id":"color-ground","type":"color","label":"Ground","default":"#0C0F12"},
    {"id":"color-ink","type":"color","label":"Text","default":"#D2D8DE"},
    {"id":"color-dim","type":"color","label":"Dim text","default":"#66717C"},
    {"id":"color-cyan","type":"color","label":"Accent (borders)","default":"#56C4D6"},
    {"id":"color-magenta","type":"color","label":"Headings","default":"#C48BE0"},
    {"id":"color-yellow","type":"color","label":"Dates and meta","default":"#CBB26A"},
    {"id":"color-blue","type":"color","label":"Links","default":"#7FA6E0"},
    {"id":"font-mono","type":"string","label":"Typeface","default":"JetBrains Mono"}
  ]'>
  <head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=1920, height=1080" />
    <script src="https://cdn.jsdelivr.net/npm/gsap@3.15.0/dist/gsap.min.js"></script>
    <style>
      :root {
        --ground: #0C0F12;
        --ink: #D2D8DE;
        --dim: #66717C;
        --cyan: #56C4D6;
        --magenta: #C48BE0;
        --yellow: #CBB26A;
        --blue: #7FA6E0;
      }
      * { margin: 0; padding: 0; box-sizing: border-box; }
      html, body { width: 1920px; height: 1080px; overflow: hidden; background: var(--ground); }
    </style>
  </head>
  <body>
    <div id="root" data-composition-id="main" data-start="0" data-duration="${total}"
      data-width="${W}" data-height="${H}" data-motion-blur="on">
${hosts}
${audioTags}
    </div>
    <script>
      window.__timelines["main"] = gsap.timeline({ paused: true });
    </script>
  </body>
</html>
`;
fs.writeFileSync(path.join(ROOT, "index.html"), index);
console.log(`wrote ${scenes.length} scenes, ${total}s`);
