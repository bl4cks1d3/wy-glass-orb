// Pre-visualizacao do orb: a mesma matematica de main/display/orb_ui.cc (StaticPixel e
// RenderGlobe), em 240x240, para o que se ve aqui ser o que a placa desenha.
"use strict";

const OrbPreview = (() => {
  const SIZE = 240, CX = 120, CY = 110, R = 86;
  const BOX = 192, BOX_X = 120 - BOX / 2, BOX_Y = 110 - BOX / 2;
  const SAMPLES = 280, TICK_MS = 50;

  const hex = (h) => { const n = parseInt(h.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; };
  const lerp = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
  const add = (a, b, k) => [a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k];
  const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
  const grad = (c0, c1, c2, mid, t) => { t = clamp(t, 0, 1); return t < mid ? lerp(c0, c1, t / mid) : lerp(c1, c2, (t - mid) / (1 - mid)); };

  class Preview {
    constructor(canvas) {
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
      this.timer = setInterval(() => this.tick(), TICK_MS);
    }

    setConfig(cfg) {
      this.cfg = cfg;
      this.buildStatic();
    }

    setState(state) { this.state = state; this.phase = 0; }
    setScreen(screen) { this.screen = screen; }

    buildStatic() {
      const g = this.cfg.globe;
      const line = hex(g.line_color), halo = hex(g.halo_color);
      const bg = g.background.map(hex), sp = g.sphere.map(hex);
      for (let y = 0; y < SIZE; y++) {
        for (let x = 0; x < SIZE; x++) {
          const px = x + 0.5, py = y + 0.5;
          let c = grad(bg[0], bg[1], bg[2], 0.45, Math.hypot(px - 120, py - 120) / 170);
          const dx = px - CX, dy = py - CY, d = Math.hypot(dx, dy);
          const k = d / (1.05 * R);
          c = add(c, halo, 0.12 * Math.exp(-k * k * 2));
          const inside = clamp(R + 0.5 - d, 0, 1);
          if (inside > 0) {
            const gx = CX - 0.3 * R, gy = CY - 0.35 * R;
            let s = grad(sp[0], sp[1], sp[2], 0.65, Math.hypot(px - gx, py - gy) / (1.3 * R));
            for (let i = -5; i <= 5; i++) {
              const lat = (i / 6) * Math.PI / 2;
              const ly = CY + Math.sin(lat) * R, rx = Math.cos(lat) * R;
              const cover = 1 - Math.abs(py - ly) / 1.1;
              if (cover > 0 && Math.abs(dx) <= rx) s = add(s, line, 0.45 * cover);
            }
            c = lerp(c, s, inside);
          }
          const edge = Math.abs(d - R);
          c = add(c, line, 0.65 * clamp(1 - edge / 0.9, 0, 1) + 0.3 * Math.exp(-edge / 3.5));
          const o = (y * SIZE + x) * 3;
          this.base[o] = Math.min(255, c[0]); this.base[o + 1] = Math.min(255, c[1]); this.base[o + 2] = Math.min(255, c[2]);
        }
      }
    }

    style() {
      return this.cfg.states[this.state] || this.cfg.states.idle;
    }

    tick() {
      if (!this.cfg) return;
      const st = this.style();
      this.angle = (this.angle + this.cfg.globe.base_speed * st.speed) % (2 * Math.PI);
      this.phase = (this.phase + TICK_MS) % st.period_ms;
      this.draw();
    }

    draw() {
      const ctx = this.ctx;
      if (this.screen === "config") return this.drawConfig();
      if (this.screen === "info") return this.drawInfo();
      this.renderGlobe();
      ctx.putImageData(this.image, 0, 0);
      this.drawOverlay();
    }

    renderGlobe() {
      const g = this.cfg.globe, st = this.style();
      const light = this.light;
      light.fill(0);
      const addL = (x, y, a) => { if (x >= 0 && y >= 0 && x < BOX && y < BOX && a > 0) light[y * BOX + x] = Math.min(1, light[y * BOX + x] + a); };
      const splat = (x, y, a) => {
        const x0 = Math.floor(x - 0.5), y0 = Math.floor(y - 0.5), fx = x - 0.5 - x0, fy = y - 0.5 - y0;
        addL(x0, y0, a * (1 - fx) * (1 - fy)); addL(x0 + 1, y0, a * fx * (1 - fy));
        addL(x0, y0 + 1, a * (1 - fx) * fy); addL(x0 + 1, y0 + 1, a * fx * fy);
      };
      const cx = CX - BOX_X, cy = CY - BOX_Y;
      for (let i = 0; i < g.meridians; i++) {
        const phi = (i / g.meridians) * Math.PI + this.angle;
        const s = Math.sin(phi), depth = Math.abs(s);
        const alpha = (0.12 + depth * 0.5) * (0.8 + depth * 0.5) * 0.55;
        for (let j = 0; j <= SAMPLES; j++) {
          const th = (j / SAMPLES) * Math.PI;
          splat(cx + R * Math.sin(th) * s, cy + R * Math.cos(th), alpha);
        }
      }
      for (let i = 0; i < g.dots; i++) {
        const theta = i * 2.399, y = 1 - (i / Math.max(1, g.dots - 1)) * 2;
        const ring = Math.sqrt(Math.max(0, 1 - y * y));
        const x = Math.cos(theta + this.angle * 0.5) * ring, z = Math.sin(theta + this.angle * 0.5) * ring;
        if (z < -0.1) continue;
        const px = cx + x * R, py = cy + y * R, rad = z > 0.7 ? 1.6 : 1.0, a = 0.3 + z * 0.6;
        for (let yy = Math.floor(py - rad - 1); yy <= Math.floor(py + rad + 1); yy++)
          for (let xx = Math.floor(px - rad - 1); xx <= Math.floor(px + rad + 1); xx++)
            addL(xx, yy, a * clamp(rad + 0.5 - Math.hypot(xx + 0.5 - px, yy + 0.5 - py), 0, 1));
      }
      const wave = Math.sin(2 * Math.PI * this.phase / st.period_ms);
      const gain = st.brightness * (1 + st.pulse * wave);
      const tint = hex(st.color).map((v) => v * gain);
      const data = this.image.data, base = this.base;
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
      const label = g.label_color;
      ctx.globalAlpha = 0.8;
      this.text(st.text, 120, 213, label, 3);
      this.text("▮ 76%", 120, 14, label, 0, 13);
      ctx.globalAlpha = 1;
      if (this.state === "listening") {
        ctx.strokeStyle = "#EF4444"; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.arc(120, 120, 117, 0, 2 * Math.PI); ctx.stroke();
      }
    }

    drawRing(color, radius, alpha) {
      const ctx = this.ctx;
      ctx.globalAlpha = alpha; ctx.strokeStyle = color; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.arc(120, 120, radius, 0, 2 * Math.PI); ctx.stroke();
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
      const lines = ["FW 0.2.0  Oct 25 2026", "ESP32-S3 r0.2  16MB/8MB", "MAC 90:70:69:f4:6d:30",
        "WiFi MinhaRede -46dBm", "IP 192.168.0.53",
        `SRV ${(this.cfg.default_server || "192.168.0.35:8733").replace(/^wss?:\/\//, "").split("/")[0]}`,
        "BAT 76%", "RAM 70K  PSRAM 5900K", "LIGADO 0:12:31"];
      lines.forEach((l, i) => this.text(l, 120, 48 + i * 15.6, "#CBD5E1", 0, 13, 400));
      ctx.strokeStyle = g.line_color; ctx.fillStyle = g.line_color + "33"; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.roundRect(62, 188, 116, 28, 14); ctx.fill(); ctx.stroke();
      this.text("CONFIGURAR", 120, 202, g.label_color, 2, 13);
    }
  }

  return { Preview, STATES: ["starting", "idle", "connecting", "listening", "speaking", "error"] };
})();
