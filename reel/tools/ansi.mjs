// tmux capture-pane -e -> grid of styled cells (the real sift screen).
const BOX = new Set([..."─│┌┐└┘├┤┬┴┼"]);
const COLORS = {
  30: "black",
  31: "red",
  32: "green",
  33: "yellow",
  34: "blue",
  35: "magenta",
  36: "cyan",
  37: "ink",
  90: "black",
  91: "red",
  92: "green",
  93: "yellow",
  94: "blue",
  95: "magenta",
  96: "cyan",
  97: "ink",
};

export function parseAnsi(src, cols, rows) {
  const grid = Array.from({ length: rows }, () =>
    Array.from({ length: cols }, () => ({ t: " ", fg: "ink", b: 0, d: 0, u: 0, r: 0 })),
  );
  let st = { fg: "ink", b: 0, d: 0, u: 0, r: 0 };
  let x = 0;
  let y = 0;
  const lines = src.split("\n");
  for (const line of lines) {
    x = 0;
    for (let i = 0; i < line.length; ) {
      const ch = line[i];
      if (ch === "\x1b") {
        const m = /^\x1b\[([0-9;]*)m/.exec(line.slice(i));
        if (m) {
          const codes = m[1] === "" ? [0] : m[1].split(";").map(Number);
          for (const c of codes) {
            if (c === 0) st = { fg: "ink", b: 0, d: 0, u: 0, r: 0 };
            else if (c === 1) st.b = 1;
            else if (c === 2) st.d = 1;
            else if (c === 4) st.u = 1;
            else if (c === 7) st.r = 1;
            else if (c === 22) { st.b = 0; st.d = 0; }
            else if (c === 24) st.u = 0;
            else if (c === 27) st.r = 0;
            else if (c === 39) st.fg = "ink";
            else if (COLORS[c]) st.fg = COLORS[c];
          }
          i += m[0].length;
          continue;
        }
        // drop any other escape (kitty graphics, mode sets)
        const s = /^\x1b[()#][0-9A-Za-z]/.exec(line.slice(i));
        if (s) { i += s[0].length; continue; }
        const csi = /^\x1b\[[0-9;?]*[A-Za-z]/.exec(line.slice(i));
        if (csi) { i += csi[0].length; continue; }
        i += 1;
        continue;
      }
      if (y < rows && x < cols) grid[y][x] = { t: ch, ...st };
      x += 1;
      i += 1;
    }
    y += 1;
  }
  return grid;
}

// Runs of cells with identical attributes, box-drawing glyphs removed.
export function textRuns(grid) {
  const runs = [];
  grid.forEach((row, y) => {
    let cur = null;
    row.forEach((c, x) => {
      const isBox = BOX.has(c.t);
      const t = isBox ? " " : c.t;
      const key = `${c.fg}|${c.b}|${c.d}|${c.u}|${c.r}`;
      if (!cur || cur.key !== key || x !== cur.x + cur.text.length) {
        cur = { x, y, key, ...c, text: "" };
        runs.push(cur);
      }
      cur.text += t;
    });
    if (cur) cur.text = cur.text.replace(/\s+$/, "");
  });
  return runs
    .map((r) => ({ ...r, text: r.text.replace(/\s+$/, "") }))
    .filter((r) => r.text.trim() !== "");
}
