// Pre-visualizacao do orb: a mesma matematica de main/display/orb_ui.cc (StaticPixel e os
// pintores de cada tipo), em 240x240, para o que se ve aqui ser o que a placa desenha.
"use strict";

const OrbPreview = (() => {
  const SIZE = 240, CX = 120, CY = 110, R = 86;
  const BOX = 192, BOX_X = 120 - BOX / 2, BOX_Y = 110 - BOX / 2;
  const BCX = CX - BOX_X, BCY = CY - BOX_Y;  // centro dentro do quadrado do globo
  const SAMPLES = 280;
  const TAU = 2 * Math.PI;

  const hex = (h) => { const n = parseInt(h.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };
  const lerp = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  const add = (a, b, k) => [a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k];
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const grad = (c0, c1, c2, mid, t) => { t = clamp(t, 0, 1); return t < mid ? lerp(c0, c1, t / mid) : lerp(c1, c2, (t - mid) / (1 - mid)); };

  // ---------------------------------------------------------------- tipos de orb
  // Mesmo registro de orb_ui.cc: o que e fixo (sphere/parallels/outline) e o pintor de cada quadro.
  // Para criar um tipo novo: docs/studio.md, "Criar um tipo de orb".
  const STYLES = {
    globe: {
      name: "Globo holográfico", description: "Meridianos girando, paralelos e pontos de luz",
      sphere: true, parallels: true, outline: true,
      paint(p) {
        const g = p.cfg.globe;
        for (let i = 0; i < g.meridians; i++) {
          const phi = (i / g.meridians) * Math.PI + p.angle;
          const s = Math.sin(phi), depth = Math.abs(s);
          const alpha = (0.12 + depth * 0.5) * (0.8 + depth * 0.5) * 0.55;
          for (let j = 0; j <= SAMPLES; j++) {
            const th = (j / SAMPLES) * Math.PI;
            p.splat(BCX + R * Math.sin(th) * s, BCY + R * Math.cos(th), alpha);
          }
        }
        for (let i = 0; i < g.dots; i++) {
          const theta = i * 2.399, y = 1 - (i / Math.max(1, g.dots - 1)) * 2;
          const ring = Math.sqrt(Math.max(0, 1 - y * y));
          const x = Math.cos(theta + p.angle * 0.5) * ring, z = Math.sin(theta + p.angle * 0.5) * ring;
          if (z < -0.1) continue;
          p.disc(BCX + x * R, BCY + y * R, z > 0.7 ? 1.6 : 1.0, 0.3 + z * 0.6);
        }
      },
    },
    rings: {
      name: "Anéis orbitais", description: "Três órbitas com cometas em volta de um núcleo",
      paint(p) {
        const A = [0.98, 0.80, 0.62], RATIO = [0.36, 0.55, 0.28], ROT = [0.35, -0.65, 1.30], SPIN = [2.4, -3.6, 5.0];
        p.glow(BCX, BCY, 9, 0.95);
        for (let o = 0; o < 3; o++) {
          const a = R * A[o], b = a * RATIO[o], cr = Math.cos(ROT[o]), sr = Math.sin(ROT[o]);
          const point = (t) => { const ex = a * Math.cos(t), ey = b * Math.sin(t); return [BCX + ex * cr - ey * sr, BCY + ex * sr + ey * cr]; };
          const n = Math.floor(TAU * a);
          for (let j = 0; j < n; j++) { const [x, y] = point(j * TAU / n); p.splat(x, y, 0.10); }
          const head = p.angle * SPIN[o] + o * 2.1, dir = SPIN[o] > 0 ? 1 : -1;
          for (let k = 0; k < 90; k++) { const w = 1 - k / 90, [x, y] = point(head - dir * k * 0.02); p.splat(x, y, 0.85 * w * w); }
          const [hx, hy] = point(head);
          p.disc(hx, hy, 1.8, 0.9);
        }
      },
    },
    plasma: {
      name: "Esfera de plasma", description: "Luz suave com ondas e redemoinho por dentro",
      sphere: true,
      paint(p) {
        if (!p.plasma) {
          p.plasma = { base: new Float32Array(BOX * BOX), ripple: new Float32Array(BOX * BOX), swirl: new Float32Array(BOX * BOX) };
          for (let y = 0; y < BOX; y++) for (let x = 0; x < BOX; x++) {
            const dx = x + 0.5 - BCX, dy = y + 0.5 - BCY, d = Math.hypot(dx, dy), k = y * BOX + x;
            const inside = clamp(R + 0.5 - d, 0, 1);
            p.plasma.base[k] = inside * (0.18 + 0.82 * Math.pow(Math.max(0, 1 - d / R), 1.8));
            p.plasma.ripple[k] = d * 0.25;
            p.plasma.swirl[k] = 3 * Math.atan2(dy, dx) + d * 0.07;
          }
        }
        const { base, ripple, swirl } = p.plasma, ra = p.angle * 4, sa = p.angle * 2.2;
        for (let k = 0; k < BOX * BOX; k++) {
          if (!base[k]) continue;
          p.light[k] = clamp(base[k] * (0.75 + 0.15 * Math.sin(ripple[k] - ra) + 0.12 * Math.sin(swirl[k] + sa)), 0, 1);
        }
      },
    },
    wave: {
      name: "Onda de voz", description: "Três ondas circulares entrelaçadas",
      paint(p) {
        const r0 = R * 0.6, a = p.angle;
        p.glow(BCX, BCY, 14, 0.35);
        for (let s = 0; s < 3; s++) {
          const alpha = 0.6 - s * 0.15;
          for (let j = 0; j < 540; j++) {
            const th = j * TAU / 540;
            const rr = r0 + 9 * Math.sin(3 * th + a * 2.2 + s * 2.1) * Math.sin(a * 0.9 + s) + 5 * Math.sin(5 * th - a * 3.3 + s * 1.3);
            p.splat(BCX + rr * Math.cos(th), BCY + rr * Math.sin(th), alpha);
          }
        }
      },
    },
    particles: {
      name: "Nuvem de partículas", description: "Esfera de pontos 3D girando com profundidade",
      sphere: true, outline: true,
      paint(p) {
        const dots = p.cfg.globe.dots, n = dots > 0 ? Math.min(dots * 3, 300) : 150, radius = R * 0.92;
        const ct = Math.cos(0.45), st = Math.sin(0.45);
        for (let i = 0; i < n; i++) {
          const y = 1 - (i / Math.max(1, n - 1)) * 2, ring = Math.sqrt(Math.max(0, 1 - y * y));
          const th = i * 2.399 + p.angle, x = Math.cos(th) * ring, z = Math.sin(th) * ring;
          const y2 = y * ct - z * st, z2 = y * st + z * ct, depth = (z2 + 1) / 2;
          p.disc(BCX + x * radius, BCY + y2 * radius, 0.6 + 1.3 * depth, 0.12 + 0.8 * depth);
        }
      },
    },
    pulse: {
      name: "Pulso minimalista", description: "Núcleo que respira e anéis que se expandem",
      paint(p) {
        const frac = p.phase / p.style().period_ms;
        const core = 14 + 4 * Math.sin(TAU * frac);
        p.glow(BCX, BCY, core * 0.6, 0.6);
        p.disc(BCX, BCY, core, 0.8);
        for (let r = 0; r < 2; r++) {
          const f = (frac + r * 0.5) % 1, w = 1 - f;
          p.circle(BCX, BCY, core + (R - core) * f, 0.55 * w * Math.sqrt(w));
        }
      },
    },
    amoeba: {
      name: "Ameba", description: "Personagem verde com olhos e boca que mudam com o estado",
      direct: true,  // pinta em cores, depois da base (nao usa a luz na cor do estado)
      paint(p) { paintAmoeba(p); },
    },
  };

  // ---------------------------------------------------------------- ameba (igual a PaintAmoeba)
  const AMOEBA = { light: "#A8F7E6", shadow: "#4FD3BA", line: "#1C2B2B", teeth: "#F4FBF9", mouth: "#7A2E3A" };
  const SPOTS = [
    [-0.10, -0.62, 0.12, "#F2876B"], [0.26, -0.74, 0.07, "#F48FB1"], [-0.58, -0.24, 0.10, "#F5A651"],
    [0.60, -0.22, 0.10, "#F5A651"], [-0.54, 0.30, 0.10, "#F5A651"], [0.58, 0.30, 0.06, "#F48FB1"],
    [-0.30, 0.58, 0.06, "#F48FB1"], [0.38, 0.56, 0.06, "#F48FB1"], [0.00, 0.50, 0.09, "#35B6C9"],
    [-0.66, 0.02, 0.04, "#3BBFA5"], [0.42, -0.48, 0.035, "#3BBFA5"],
  ];
  const bump = (th, center, width, height) => {
    let d = (th - center) % TAU;
    if (d > Math.PI) d -= TAU; else if (d < -Math.PI) d += TAU;
    return height * Math.exp(-(d * d) / (width * width));
  };

  function paintAmoeba(p) {
    const st = p.style(), frac = p.phase / st.period_ms, breath = Math.sin(TAU * frac);
    const body = R * 0.74 * (1 + 0.06 * st.pulse * breath);
    const rt = new Float32Array(256);
    for (let i = 0; i < 256; i++) {
      const th = i * TAU / 256;
      rt[i] = body * (1 + 0.05 * Math.sin(7 * th + p.angle * 1.5) + 0.03 * Math.sin(3 * th - p.angle * 2))
        + bump(th, 0.30, 0.16, 13) + bump(th, Math.PI - 0.30, 0.16, 13)
        + bump(th, 1.30, 0.13, 11) + bump(th, Math.PI - 1.30, 0.13, 11);
    }
    const light = hex(AMOEBA.light), shadow = hex(AMOEBA.shadow), line = hex(AMOEBA.line);
    for (let y = 0; y < BOX; y++) for (let x = 0; x < BOX; x++) {
      const dx = x + 0.5 - BCX, dy = y + 0.5 - BCY, d = Math.hypot(dx, dy);
      const r = rt[Math.round(Math.atan2(dy, dx) * 256 / TAU) & 255];
      if (d > r + 1) continue;
      const shade = clamp(0.55 - (dx * 0.45 + dy * 0.6) / (2 * body), 0, 1);
      let c = lerp(shadow, light, shade);
      c = lerp(c, line, clamp(3.2 - (r - d), 0, 1));
      p.blendRgb(x, y, c, clamp(r + 1 - d, 0, 1));
    }
    for (const [sx, sy, sr, color] of SPOTS) {
      p.dot(BCX + sx * body, BCY + sy * body, sr * body + 1.2, AMOEBA.line);
      p.dot(BCX + sx * body, BCY + sy * body, sr * body, color);
    }
    const eyeY = BCY - 0.14 * body, eyeDx = 0.30 * body, mouthY = BCY + 0.12 * body;
    const happyEye = (ex) => { for (let i = -8; i <= 8; i++) { const t = i / 8; p.dot(ex + t * 0.12 * body, eyeY + 0.06 * body * t * t, 1.6, AMOEBA.line); } };
    const openEye = (ex, look) => {
      p.ellipse(ex + look, eyeY, 0.065 * body, 0.10 * body, AMOEBA.line);
      p.dot(ex + look - 0.02 * body, eyeY - 0.035 * body, 0.025 * body, "#FFFFFF");
    };
    const xEye = (ex) => { for (let i = -6; i <= 6; i++) { const t = i / 6 * 0.07 * body; p.dot(ex + t, eyeY + t, 1.5, AMOEBA.line); p.dot(ex + t, eyeY - t, 1.5, AMOEBA.line); } };
    const smile = (open) => {
      const rx = 0.27 * body, ry = (0.06 + 0.16 * open) * body;
      p.ellipse(BCX, mouthY, rx, ry, AMOEBA.line, 1);
      p.ellipse(BCX, mouthY, rx - 2.5, Math.max(1, ry - 2.5), AMOEBA.mouth, 1);
      p.ellipse(BCX, mouthY, rx - 2.5, Math.max(1, (ry - 2.5) * 0.38), AMOEBA.teeth, 1);
    };
    switch (p.state) {
      case "listening":
        openEye(BCX - eyeDx, 0); openEye(BCX + eyeDx, 0);
        p.ellipse(BCX, mouthY + 0.05 * body, 0.07 * body, 0.08 * body, AMOEBA.line);
        p.ellipse(BCX, mouthY + 0.05 * body, 0.045 * body, 0.055 * body, AMOEBA.mouth);
        break;
      case "speaking":
        happyEye(BCX - eyeDx); happyEye(BCX + eyeDx);
        smile(0.5 + 0.5 * Math.sin(TAU * frac * 2));
        break;
      case "connecting": {
        const look = 0.04 * body * Math.sin(p.angle * 3);
        openEye(BCX - eyeDx, look); openEye(BCX + eyeDx, look);
        for (let i = -6; i <= 6; i++) p.dot(BCX + i * 0.02 * body, mouthY + 0.04 * body, 1.5, AMOEBA.line);
        break;
      }
      case "error":
        xEye(BCX - eyeDx); xEye(BCX + eyeDx);
        for (let i = -8; i <= 8; i++) { const t = i / 8; p.dot(BCX + t * 0.16 * body, mouthY + 0.10 * body - 0.06 * body * (1 - t * t), 1.6, AMOEBA.line); }
        break;
      default:
        happyEye(BCX - eyeDx); happyEye(BCX + eyeDx);
        smile(1);
    }
  }

  class Preview {
    // options: { style, tickMs, overlay } — a galeria usa um estilo fixo, mais lento, sem textos
    constructor(canvas, options = {}) {
      this.canvas = canvas;
      this.ctx = canvas.getContext("2d");
      this.image = this.ctx.createImageData(SIZE, SIZE);
      this.base = new Float32Array(SIZE * SIZE * 3);
      this.light = new Float32Array(BOX * BOX);
      this.angle = 0;
      this.phase = 0;
      this.state = "idle";
      this.screen = "globe";
      this.cfg = null;
      this.fixedStyle = options.style || null;
      this.overlay = options.overlay !== false;
      this.tickMs = options.tickMs || 50;
      this.timer = setInterval(() => this.tick(), this.tickMs);
    }

    styleId() { return this.fixedStyle || this.cfg.style || "globe"; }
    styleDef() { return STYLES[this.styleId()] || STYLES.globe; }

    setConfig(cfg) {
      this.cfg = cfg;
      this.buildStatic();
    }

    refreshStyle() {
      if (this.builtFor !== this.styleId()) this.buildStatic();
    }

    setState(state) { this.state = state; this.phase = 0; }
    setScreen(screen) { this.screen = screen; }

    buildStatic() {
      const g = this.cfg.globe, def = this.styleDef();
      this.builtFor = this.styleId();
      const line = hex(g.line_color), halo = hex(g.halo_color);
      const bg = g.background.map(hex), sp = g.sphere.map(hex);
      for (let y = 0; y < SIZE; y++) {
        for (let x = 0; x < SIZE; x++) {
          const px = x + 0.5, py = y + 0.5;
          let c = grad(bg[0], bg[1], bg[2], 0.45, Math.hypot(px - 120, py - 120) / 170);
          const dx = px - CX, dy = py - CY, d = Math.hypot(dx, dy);
          const k = d / (1.05 * R);
          c = add(c, halo, 0.12 * Math.exp(-k * k * 2));
          const inside = def.sphere ? clamp(R + 0.5 - d, 0, 1) : 0;
          if (inside > 0) {
            const gx = CX - 0.3 * R, gy = CY - 0.35 * R;
            let s = grad(sp[0], sp[1], sp[2], 0.65, Math.hypot(px - gx, py - gy) / (1.3 * R));
            for (let i = -5; def.parallels && i <= 5; i++) {
              const lat = (i / 6) * Math.PI / 2;
              const ly = CY + Math.sin(lat) * R, rx = Math.cos(lat) * R;
              const cover = 1 - Math.abs(py - ly) / 1.1;
              if (cover > 0 && Math.abs(dx) <= rx) s = add(s, line, 0.45 * cover);
            }
            c = lerp(c, s, inside);
          }
          if (def.outline) {
            const edge = Math.abs(d - R);
            c = add(c, line, 0.65 * clamp(1 - edge / 0.9, 0, 1) + 0.3 * Math.exp(-edge / 3.5));
          }
          const o = (y * SIZE + x) * 3;
          this.base[o] = Math.min(255, c[0]); this.base[o + 1] = Math.min(255, c[1]); this.base[o + 2] = Math.min(255, c[2]);
        }
      }
    }

    style() {
      return this.cfg.states[this.state] || this.cfg.states.idle;
    }

    tick() {
      if (!this.cfg || !this.canvas.isConnected) return;
      this.refreshStyle();
      const st = this.style();
      // a galeria anda mais devagar por tick: compensa para girar no mesmo ritmo
      const steps = this.tickMs / 50;
      this.angle = (this.angle + this.cfg.globe.base_speed * st.speed * steps) % TAU;
      this.phase = (this.phase + this.tickMs) % st.period_ms;
      this.draw();
    }

    draw() {
      if (this.screen === "config") return this.drawConfig();
      if (this.screen === "info") return this.drawInfo();
      this.renderGlobe();
      this.ctx.putImageData(this.image, 0, 0);
      if (this.overlay) this.drawOverlay();
    }

    // ---- primitivas de luz (as mesmas de orb_ui.cc)
    addLight(x, y, a) {
      if (x >= 0 && y >= 0 && x < BOX && y < BOX && a > 0) this.light[y * BOX + x] = Math.min(1, this.light[y * BOX + x] + a);
    }
    splat(x, y, a) {
      const x0 = Math.floor(x - 0.5), y0 = Math.floor(y - 0.5), fx = x - 0.5 - x0, fy = y - 0.5 - y0;
      this.addLight(x0, y0, a * (1 - fx) * (1 - fy)); this.addLight(x0 + 1, y0, a * fx * (1 - fy));
      this.addLight(x0, y0 + 1, a * (1 - fx) * fy); this.addLight(x0 + 1, y0 + 1, a * fx * fy);
    }
    disc(px, py, rad, a) {
      for (let yy = Math.floor(py - rad - 1); yy <= Math.floor(py + rad + 1); yy++)
        for (let xx = Math.floor(px - rad - 1); xx <= Math.floor(px + rad + 1); xx++)
          this.addLight(xx, yy, a * clamp(rad + 0.5 - Math.hypot(xx + 0.5 - px, yy + 0.5 - py), 0, 1));
    }
    glow(px, py, sigma, peak) {
      const r = Math.floor(sigma * 3), k = 1 / (2 * sigma * sigma);
      for (let yy = Math.floor(py) - r; yy <= Math.floor(py) + r; yy++)
        for (let xx = Math.floor(px) - r; xx <= Math.floor(px) + r; xx++) {
          const dx = xx + 0.5 - px, dy = yy + 0.5 - py;
          this.addLight(xx, yy, peak * Math.exp(-(dx * dx + dy * dy) * k));
        }
    }
    // ---- pintura em cores, direto na imagem (coordenadas do quadrado do globo)
    blendRgb(x, y, rgb, a) {
      if (x < 0 || y < 0 || x >= BOX || y >= BOX || a <= 0) return;
      const d = this.image.data, o = ((y + BOX_Y) * SIZE + (x + BOX_X)) * 4, k = Math.min(1, a);
      d[o] += (rgb[0] - d[o]) * k; d[o + 1] += (rgb[1] - d[o + 1]) * k; d[o + 2] += (rgb[2] - d[o + 2]) * k;
    }
    dot(px, py, rad, color, a = 1) {
      const rgb = hex(color);
      for (let yy = Math.floor(py - rad - 1); yy <= Math.floor(py + rad + 1); yy++)
        for (let xx = Math.floor(px - rad - 1); xx <= Math.floor(px + rad + 1); xx++)
          this.blendRgb(xx, yy, rgb, a * clamp(rad + 0.5 - Math.hypot(xx + 0.5 - px, yy + 0.5 - py), 0, 1));
    }
    ellipse(px, py, rx, ry, color, half = 0) {
      const rgb = hex(color);
      for (let yy = Math.floor(py - ry - 1); yy <= Math.floor(py + ry + 1); yy++) {
        const fy = yy + 0.5 - py;
        if ((half > 0 && fy < 0) || (half < 0 && fy > 0)) continue;
        for (let xx = Math.floor(px - rx - 1); xx <= Math.floor(px + rx + 1); xx++) {
          const fx = xx + 0.5 - px, e = Math.sqrt((fx * fx) / (rx * rx) + (fy * fy) / (ry * ry));
          this.blendRgb(xx, yy, rgb, clamp((1 - e) * Math.min(rx, ry) + 0.5, 0, 1));
        }
      }
    }
    circle(px, py, radius, a) {
      const n = Math.max(24, Math.floor(TAU * radius));
      for (let j = 0; j < n; j++) { const t = j * TAU / n; this.splat(px + radius * Math.cos(t), py + radius * Math.sin(t), a); }
    }

    renderGlobe() {
      const st = this.style();
      this.light.fill(0);
      const def = this.styleDef();
      if (!def.direct) def.paint(this);
      const wave = Math.sin(TAU * this.phase / st.period_ms);
      const gain = st.brightness * (1 + st.pulse * wave);
      const tint = hex(st.color).map((v) => v * gain);
      const data = this.image.data, base = this.base, light = this.light;
      for (let y = 0; y < SIZE; y++) {
        for (let x = 0; x < SIZE; x++) {
          const o = (y * SIZE + x) * 3, p = (y * SIZE + x) * 4;
          const bx = x - BOX_X, by = y - BOX_Y;
          const l = bx >= 0 && by >= 0 && bx < BOX && by < BOX ? light[by * BOX + bx] : 0;
          data[p] = Math.min(255, base[o] + l * tint[0]);
          data[p + 1] = Math.min(255, base[o + 1] + l * tint[1]);
          data[p + 2] = Math.min(255, base[o + 2] + l * tint[2]);
          data[p + 3] = 255;
        }
      }
      if (def.direct) def.paint(this);
    }

    text(str, x, y, color, spacing = 0, size = 14, weight = 500) {
      const ctx = this.ctx;
      ctx.font = `${weight} ${size}px Montserrat, "Segoe UI", system-ui, sans-serif`;
      ctx.letterSpacing = `${spacing}px`;
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillStyle = color;
      // Como no firmware: a fonte da placa so tem ASCII, os acentos somem
      ctx.fillText(String(str).normalize("NFKD").replace(/[̀-ͯ]/g, ""), x, y);
      ctx.letterSpacing = "0px";
    }

    drawOverlay() {
      const ctx = this.ctx, g = this.cfg.globe, st = this.style();
      ctx.globalAlpha = 0.8;
      this.text(st.text, 120, 213, g.label_color, 3);
      this.text("▮ 76%", 120, 14, g.label_color, 0, 13);
      ctx.globalAlpha = 1;
      if (this.state === "listening") {
        ctx.strokeStyle = "#EF4444"; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.arc(120, 120, 117, 0, TAU); ctx.stroke();
      }
    }

    drawRing(color, radius, alpha) {
      const ctx = this.ctx;
      ctx.globalAlpha = alpha; ctx.strokeStyle = color; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(120, 120, radius, 0, TAU); ctx.stroke();
      ctx.globalAlpha = 1;
    }

    drawConfig() {
      const ctx = this.ctx, g = this.cfg.globe;
      ctx.fillStyle = "#020711"; ctx.fillRect(0, 0, SIZE, SIZE);
      this.drawRing(g.line_color, 114, 0.6);
      this.text("Configurar Wi-Fi", 120, 38, "#ffffff", 1);
      // QR ilustrativo (na placa e o QR real da rede do portal)
      ctx.fillStyle = "#fff"; ctx.fillRect(62, 54, 116, 116);
      ctx.fillStyle = "#000";
      let seed = 7;
      for (let yy = 0; yy < 25; yy++) for (let xx = 0; xx < 25; xx++) {
        seed = (seed * 1103515245 + 12345) & 0x7fffffff;
        const finder = (xx < 7 && yy < 7) || (xx > 17 && yy < 7) || (xx < 7 && yy > 17);
        const on = finder ? (xx % 6 === 0 || yy % 6 === 0 || (xx % 6 > 1 && xx % 6 < 5 && yy % 6 > 1 && yy % 6 < 5)) || (xx > 17 && (xx - 18) % 6 === 0) : seed % 3 === 0;
        if (on) ctx.fillRect(68 + xx * 4.16, 60 + yy * 4.16, 4.2, 4.2);
      }
      this.text(`${this.cfg.ap_prefix}-6D31`, 120, 188, g.label_color, 1);
      this.text("Abra 192.168.4.1", 120, 205, "#94A3B8", 0);
    }

    drawInfo() {
      const ctx = this.ctx, g = this.cfg.globe;
      ctx.fillStyle = "#020711"; ctx.fillRect(0, 0, SIZE, SIZE);
      this.drawRing(g.line_color, 116, 0.4);
      this.text(this.cfg.title, 120, 28, g.label_color, 3);
      const lines = ["FW 0.2.0  Oct 26 2026", "ESP32-S3 r0.2  16MB/8MB", "MAC 90:70:69:f4:6d:30",
        "WiFi MinhaRede -46dBm", "IP 192.168.0.53",
        `SRV ${(this.cfg.default_server || "192.168.0.35:8733").replace(/^wss?:\/\//, "").split("/")[0]}`,
        "BAT 76%", "RAM 70K  PSRAM 5900K", "LIGADO 0:12:31"];
      lines.forEach((l, i) => this.text(l, 120, 48 + i * 15.6, "#CBD5E1", 0, 13, 400));
      ctx.strokeStyle = g.line_color; ctx.fillStyle = g.line_color + "33"; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.roundRect(62, 188, 116, 28, 14); ctx.fill(); ctx.stroke();
      this.text("CONFIGURAR", 120, 202, g.label_color, 2, 13);
    }
  }

  return { Preview, STYLES, STATES: ["starting", "idle", "connecting", "listening", "speaking", "error"] };
})();
