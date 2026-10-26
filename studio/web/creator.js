// Criador e Biblioteca de orbs: fontes em JS (function draw) ou HTML/CSS, renderizadas em quadros
// de 192x192 e empacotadas em .orb (docs/formato-orb.md). As fontes ficam em studio/orbs/.
"use strict";

const OrbSource = (() => {
  const SIZE = 192, BOX_X = 24, BOX_Y = 14, TAU = Math.PI * 2;
  const STATES = ["starting", "idle", "connecting", "listening", "speaking", "error"];
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const hex = (h) => { const n = parseInt(h.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };

  // Ajudantes disponiveis no codigo JS como `orb`
  const HELPERS = Object.freeze({
    SIZE, TAU, clamp,
    lerp: (a, b, k) => a + (b - a) * k,
    wave: (t, n = 1, phase = 0) => Math.sin(TAU * (t * n + phase)),   // -1..1, fecha o laco se n for inteiro
    ping: (t, n = 1) => 0.5 - 0.5 * Math.cos(TAU * t * n),            // 0..1..0
    ease: (k) => k * k * (3 - 2 * k),
    circle(ctx, x, y, r, fill) { ctx.beginPath(); ctx.arc(x, y, r, 0, TAU); ctx.fillStyle = fill; ctx.fill(); },
  });

  // O mesmo gradiente de fundo que a placa desenha (PackBackgroundPixel), so o recorte do quadro
  const bgCache = new Map();
  function background(colors, full = false) {
    const key = colors.join() + full;
    if (bgCache.has(key)) return bgCache.get(key);
    const size = full ? 240 : SIZE, ox = full ? 0 : BOX_X, oy = full ? 0 : BOX_Y;
    const img = new ImageData(size, size), [c0, c1, c2] = colors.map(hex);
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
      const t = clamp(Math.hypot(x + ox + 0.5 - 120, y + oy + 0.5 - 120) / 170, 0, 1);
      const [a, b, k] = t < 0.45 ? [c0, c1, t / 0.45] : [c1, c2, (t - 0.45) / 0.55];
      const o = (y * size + x) * 4;
      img.data[o] = a[0] + (b[0] - a[0]) * k; img.data[o + 1] = a[1] + (b[1] - a[1]) * k;
      img.data[o + 2] = a[2] + (b[2] - a[2]) * k; img.data[o + 3] = 255;
    }
    bgCache.set(key, img);
    return img;
  }

  // HTML do autor -> XHTML valido para o <foreignObject>
  function toXhtml(html) {
    const doc = new DOMParser().parseFromString(`<body>${html}</body>`, "text/html");
    const ser = new XMLSerializer();
    return [...doc.body.childNodes].map((n) => ser.serializeToString(n)).join("");
  }

  class Renderer {
    constructor(src) { this.setSource(src); }

    setSource(src) {
      this.src = src;
      this.error = null;
      this.draw = null;
      this.markup = "";
      try {
        if (src.mode === "js") {
          const factory = new Function("orb", `"use strict";\n${src.js}\n;return typeof draw === "function" ? draw : null;`);
          this.draw = factory(HELPERS);
          if (!this.draw) throw new Error("defina function draw(ctx, t, state)");
        } else {
          this.markup = toXhtml(src.html || "");
        }
      } catch (e) {
        this.error = e;
      }
    }

    // Desenha o quadro do instante t (0..1 do laco) no estado pedido, em ctx (192x192)
    async frame(ctx, state, t) {
      ctx.putImageData(background(this.src.background), 0, 0);
      if (this.error) throw this.error;
      const time = t * this.src.seconds;
      if (this.src.mode === "js") {
        ctx.save();
        try { this.draw(ctx, t, state, time); } finally { ctx.restore(); }
        return;
      }
      // Animacoes CSS congeladas no instante do quadro; --t (0..1) e --time tambem servem para animar
      const css = `${this.src.css || ""}\n.orb,.orb *,.orb *::before,.orb *::after{animation-play-state:paused!important;animation-delay:-${time.toFixed(4)}s!important}`;
      const body = `<div xmlns="http://www.w3.org/1999/xhtml" class="orb state-${state}" style="width:${SIZE}px;height:${SIZE}px;position:relative;overflow:hidden;--t:${t.toFixed(4)};--time:${time.toFixed(4)}s"><style><![CDATA[${css.replace(/]]>/g, "")}]]></style>${this.markup}</div>`;
      const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="${SIZE}" height="${SIZE}"><foreignObject width="100%" height="100%">${body}</foreignObject></svg>`;
      const img = new Image();
      img.src = "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg);
      await img.decode();
      ctx.drawImage(img, 0, 0);
    }
  }

  // Quadros JPEG de todos os estados, prontos para o servidor montar o .orb
  async function renderFrames(src, onProgress) {
    const r = new Renderer(src);
    if (r.error) throw r.error;
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = SIZE;
    const ctx = canvas.getContext("2d"), frames = {};
    const count = Math.max(1, Math.round(src.fps * src.seconds));
    let done = 0;
    for (const state of STATES) {
      frames[state] = [];
      for (let i = 0; i < count; i++) {
        await r.frame(ctx, state, i / count);
        frames[state].push(canvas.toDataURL("image/jpeg", 0.85));
        onProgress?.(++done / (count * STATES.length));
      }
    }
    return frames;
  }

  return { Renderer, renderFrames, background, STATES, SIZE, BOX_X, BOX_Y, HELPERS };
})();

// ------------------------------------------------------------------ criador e biblioteca

const Creator = (() => {
  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const STATE_LABEL = { starting: "Iniciando", idle: "Pronto", connecting: "Conectando", listening: "Ouvindo", speaking: "Falando", error: "Erro" };

  const TEMPLATES = {
    js: {
      mode: "js", name: "Novo orb", description: "", fps: 15, seconds: 2, html: "", css: "",
      js: `// draw(ctx, t, state, time): quadro de 192x192, centro em (96, 96).
// t vai de 0 a 1 ao longo do laco (use orb.wave(t) para animar sem emenda).
// state: starting, idle, connecting, listening, speaking, error
// orb: wave, ping, lerp, clamp, ease, circle(ctx, x, y, r, cor), TAU
function draw(ctx, t, state) {
  const r = 48 + 6 * orb.wave(t, state === "speaking" ? 4 : 1);
  const cor = { listening: "#34D399", speaking: "#A78BFA", error: "#EF4444" }[state] || "#38BDF8";
  ctx.shadowColor = cor;
  ctx.shadowBlur = 24;
  orb.circle(ctx, 96, 96, r, cor);
}
`,
    },
    css: {
      mode: "css", name: "Novo orb", description: "", fps: 15, seconds: 2, js: "",
      html: `<div class="bola"></div>`,
      css: `/* A raiz tem a classe do estado: .state-idle, .state-listening, .state-speaking...
   Animacoes CSS com duracao que divida o laco (ex.: 2s, 1s, 0.5s) fecham sem emenda.
   Tambem existem as variaveis --t (0 a 1 no laco) e --time (segundos). */
.bola {
  position: absolute; left: 46px; top: 46px; width: 100px; height: 100px; border-radius: 50%;
  background: radial-gradient(circle at 35% 30%, #a5f3fc, #0284c7 70%);
  box-shadow: 0 0 30px #38bdf8;
  animation: respira 2s ease-in-out infinite;
}
.state-listening .bola { background: radial-gradient(circle at 35% 30%, #bbf7d0, #059669 70%); }
.state-speaking .bola { animation-duration: 0.5s; }
.state-error .bola { background: radial-gradient(circle at 35% 30%, #fecaca, #dc2626 70%); }
@keyframes respira { 50% { transform: scale(1.12); } }
`,
    },
  };

  let api, toast, sources = [], src = null, renderer = null, state = "idle", saved = true;
  let previewCtx, frameCanvas, frameCtx, start = performance.now(), busy = false, lastDraw = 0;

  function field(id) { return $(id); }

  function load(source) {
    src = JSON.parse(JSON.stringify(source));
    src.background = src.background || ["#071B2C", "#020711", "#010208"];
    renderer = new OrbSource.Renderer(src);
    saved = !!sources.find((s) => s.id === src.id);
    syncForm();
    renderStateButtons();
    showError();
  }

  function syncForm() {
    field("cr-id").value = src.id || "";
    field("cr-name").value = src.name || "";
    field("cr-desc").value = src.description || "";
    field("cr-fps").value = src.fps;
    field("cr-seconds").value = src.seconds;
    ["cr-bg0", "cr-bg1", "cr-bg2"].forEach((id, i) => (field(id).value = src.background[i].toLowerCase()));
    field("cr-js").value = src.js || "";
    field("cr-html").value = src.html || "";
    field("cr-css").value = src.css || "";
    $("cr-js-box").hidden = src.mode !== "js";
    $("cr-css-box").hidden = src.mode !== "css";
    $("cr-mode").textContent = src.mode === "js" ? "JavaScript (canvas)" : "HTML + CSS";
    $("cr-delete").hidden = !saved;
    $("cr-sources").value = saved ? src.id : "";
  }

  function readForm() {
    src.id = field("cr-id").value.trim().toLowerCase();
    src.name = field("cr-name").value;
    src.description = field("cr-desc").value;
    src.fps = Number(field("cr-fps").value) || 15;
    src.seconds = Number(field("cr-seconds").value) || 2;
    src.background = ["cr-bg0", "cr-bg1", "cr-bg2"].map((id) => field(id).value.toUpperCase());
    src.js = field("cr-js").value;
    src.html = field("cr-html").value;
    src.css = field("cr-css").value;
  }

  let compileTimer = null;
  function onEdit() {
    readForm();
    clearTimeout(compileTimer);
    compileTimer = setTimeout(() => { renderer.setSource(src); showError(); }, 250);
  }

  function showError(err) {
    const e = err || renderer?.error;
    $("cr-error").textContent = e ? `⚠ ${e.message || e}` : "";
    $("cr-error").hidden = !e;
  }

  function renderStateButtons() {
    const bar = $("cr-states");
    bar.innerHTML = "";
    for (const st of OrbSource.STATES) {
      const b = document.createElement("button");
      b.textContent = STATE_LABEL[st];
      b.setAttribute("aria-pressed", st === state);
      b.addEventListener("click", () => { state = st; start = performance.now(); renderStateButtons(); });
      bar.appendChild(b);
    }
  }

  // Previa: tela de 240x240 como na placa (fundo, quadro do orb em (24, 14), texto do estado)
  async function loop(now) {
    requestAnimationFrame(loop);
    if (!src || busy || !$("tab-creator").classList.contains("active")) return;
    if (src.mode === "css" && now - lastDraw < 1000 / src.fps) return;  // CSS e mais caro: no fps do orb
    lastDraw = now;
    busy = true;
    const t = (((now - start) / 1000) % src.seconds) / src.seconds;
    try {
      await renderer.frame(frameCtx, state, t);
      previewCtx.putImageData(OrbSource.background(src.background, true), 0, 0);
      previewCtx.drawImage(frameCanvas, OrbSource.BOX_X, OrbSource.BOX_Y);
      const label = (typeof config !== "undefined" && config?.states?.[state]?.text) || STATE_LABEL[state].toUpperCase();
      previewCtx.font = '500 14px Montserrat, "Segoe UI", system-ui, sans-serif';
      previewCtx.letterSpacing = "3px";
      previewCtx.textAlign = "center";
      previewCtx.textBaseline = "middle";
      previewCtx.fillStyle = (typeof config !== "undefined" && config?.globe?.label_color) || "#48DFFF";
      previewCtx.globalAlpha = 0.8;
      previewCtx.fillText(String(label).normalize("NFKD").replace(/[̀-ͯ]/g, ""), 120, 213);
      previewCtx.globalAlpha = 1;
      previewCtx.letterSpacing = "0px";
      if (!renderer.error) showError();
    } catch (e) {
      showError(e);
    } finally {
      busy = false;
    }
  }

  async function save() {
    readForm();
    const { source, sources: list } = await api("/api/sources", { source: src });
    sources = list;
    saved = true;
    renderSourceList();
    load(source);
    toast(`Salvo em studio/orbs/${source.id}.json`);
    Library.render();
    return source;
  }

  async function generate(source, onProgress) {
    const frames = await OrbSource.renderFrames(source, onProgress);
    const { orb } = await api("/api/orbs", { id: source.id, name: source.name, fps: source.fps, background: source.background, frames });
    return orb;
  }

  async function generateCurrent() {
    const source = await save();
    const btn = $("cr-generate");
    btn.disabled = true;
    try {
      const orb = await generate(source, (p) => (btn.textContent = `Gerando ${Math.round(p * 100)}%`));
      toast(`Gerado studio/library/${orb.id}.orb: ${orb.frames} quadros, ${orb.kb} KB`);
      refreshOrbs?.();
    } catch (e) {
      showError(e);
    } finally {
      btn.disabled = false;
      btn.textContent = "Gerar .orb";
    }
  }

  function renderSourceList() {
    $("cr-sources").innerHTML = `<option value="">— escolha um orb seu —</option>` +
      sources.map((s) => `<option value="${esc(s.id)}">${esc(s.name)} (${s.mode.toUpperCase()})</option>`).join("");
  }

  function bind() {
    for (const id of ["cr-id", "cr-name", "cr-desc", "cr-fps", "cr-seconds", "cr-bg0", "cr-bg1", "cr-bg2", "cr-js", "cr-html", "cr-css"]) {
      field(id).addEventListener("input", onEdit);
    }
    // Tab no editor insere espacos em vez de trocar de campo
    for (const id of ["cr-js", "cr-html", "cr-css"]) {
      field(id).addEventListener("keydown", (e) => {
        if (e.key !== "Tab") return;
        e.preventDefault();
        const el = e.target, s = el.selectionStart;
        el.setRangeText("  ", s, el.selectionEnd, "end");
        onEdit();
      });
    }
    $("cr-sources").addEventListener("change", (e) => {
      const found = sources.find((s) => s.id === e.target.value);
      if (found) load(found);
    });
    $("cr-new-js").addEventListener("click", () => load({ ...TEMPLATES.js, id: "" }));
    $("cr-new-css").addEventListener("click", () => load({ ...TEMPLATES.css, id: "" }));
    $("cr-save").addEventListener("click", () => save().catch(() => {}));
    $("cr-generate").addEventListener("click", () => generateCurrent().catch(() => {}));
    $("cr-use").addEventListener("click", async (e) => {
      const source = await save().catch(() => null);
      if (source) installOrb(source.id, source.name, e.target, () => generate(source));
    });
    $("cr-delete").addEventListener("click", async () => {
      if (!confirm(`Apagar a fonte ${src.id}? O .orb já gerado continua na biblioteca.`)) return;
      sources = (await api("/api/sources/delete", { id: src.id })).sources;
      renderSourceList();
      load({ ...TEMPLATES.js, id: "" });
      Library.render();
    });
  }

  async function init(apiFn, toastFn) {
    api = apiFn;
    toast = toastFn;
    previewCtx = $("cr-preview").getContext("2d");
    frameCanvas = document.createElement("canvas");
    frameCanvas.width = frameCanvas.height = OrbSource.SIZE;
    frameCtx = frameCanvas.getContext("2d");
    bind();
    sources = (await api("/api/sources")).sources;
    renderSourceList();
    load(sources[0] || { ...TEMPLATES.js, id: "" });
    requestAnimationFrame(loop);
  }

  function edit(id) {
    const found = sources.find((s) => s.id === id);
    if (!found) return;
    load(found);
    document.querySelector('nav button[data-tab="creator"]').click();
  }

  return { init, edit, generate, get sources() { return sources; }, reloadSources: async () => { sources = (await api("/api/sources")).sources; renderSourceList(); } };
})();

const Library = (() => {
  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  let minis = [], natives = [], timer = null, filter = "";

  // Miniaturas das fontes do Criador: animadas so quando visiveis, a ~8 qps
  const visible = new Set();
  const observer = new IntersectionObserver((entries) => {
    for (const e of entries) e.isIntersecting ? visible.add(e.target) : visible.delete(e.target);
  });

  function tick() {
    if (!$("tab-library").classList.contains("active")) return;
    const now = performance.now();
    for (const m of minis) {
      if (!visible.has(m.canvas) || m.busy) continue;
      m.busy = true;
      const t = ((now / 1000) % m.src.seconds) / m.src.seconds;
      m.renderer.frame(m.ctx, "idle", t).catch(() => {}).finally(() => (m.busy = false));
    }
  }

  function card({ id, name, description, kind, current }) {
    const el = document.createElement("div");
    el.className = "lib-card";
    el.innerHTML = `<canvas width="${kind === "native" ? 240 : 192}" height="${kind === "native" ? 240 : 192}"></canvas>
      <b>${esc(name)}</b><span>${esc(description || "")}</span>
      <small>${kind === "native" ? "nativo" : kind.toUpperCase()}${current ? ' · <em>● no orb</em>' : ""}</small>
      <div class="actions"><button class="btn primary use" type="button">Usar no orb</button>
      ${kind !== "native" ? '<button class="btn edit" type="button">Editar</button>' : ""}</div>`;
    return el;
  }

  function render() {
    const grid = $("lib-grid");
    if (!grid) return;
    minis.forEach((m) => observer.unobserve(m.canvas));
    natives.forEach((p) => clearInterval(p.timer));
    minis = [];
    natives = [];
    grid.innerHTML = "";
    const query = filter.toLowerCase();
    const matches = (s) => !query || `${s.name} ${s.description || ""} ${s.id}`.toLowerCase().includes(query);

    for (const src of Creator.sources.filter(matches)) {
      const el = card({ ...src, kind: src.mode, current: currentOrb === src.id });
      grid.appendChild(el);
      const canvas = el.querySelector("canvas");
      minis.push({ canvas, ctx: canvas.getContext("2d"), src, renderer: new OrbSource.Renderer(src), busy: false });
      observer.observe(canvas);
      el.querySelector(".use").addEventListener("click", (e) => installOrb(src.id, src.name, e.target, () => Creator.generate(src)));
      el.querySelector(".edit").addEventListener("click", () => Creator.edit(src.id));
    }
    for (const [id, def] of Object.entries(OrbPreview.STYLES)) {
      if (!matches({ id, name: def.name, description: def.description })) continue;
      const el = card({ id, name: def.name, description: def.description, kind: "native", current: currentOrb === id });
      grid.appendChild(el);
      const p = new OrbPreview.Preview(el.querySelector("canvas"), { style: id, tickMs: 120, overlay: false });
      p.setConfig(config);
      natives.push(p);
      el.querySelector(".use").addEventListener("click", (e) => useOrb(id, e.target));
    }
    $("lib-count").textContent = `${grid.children.length} orbs`;
    clearInterval(timer);
    timer = setInterval(tick, 125);
  }

  function init() {
    $("lib-search").addEventListener("input", (e) => { filter = e.target.value; render(); });
    $("lib-new").addEventListener("click", () => document.querySelector('nav button[data-tab="creator"]').click());
    render();
  }

  return { init, render };
})();
