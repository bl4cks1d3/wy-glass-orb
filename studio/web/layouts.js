// Editor das telas interativas (layout JSON v1 do self.screen.render, docs/screen-layout.md).
// A placa empilha os elementos numa ordem fixa; aqui so caixas e botoes podem ser reordenados.
"use strict";

const LayoutEditor = (() => {
  const LIMITS = { id: 40, title: 60, body: 120, label: 40, items: 6, buttons: 3 };
  const KIND_LABEL = { title: "Título", body: "Texto", item: "Caixa de seleção", button: "Botão", progress: "Progresso", timer: "Timer" };
  const BUTTON_COLORS = { primary: ["#085041", "#9FE1CB"], danger: ["#501313", "#F7C1C1"], default: ["#26215C", "#CECBF6"] };

  const $ = (id) => document.getElementById(id);
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const clip = (s, n) => (typeof s === "string" ? s.slice(0, n) : "");

  function blank() {
    return { v: 1, id: "nova-tela", title: "Confirmar?", buttons: [{ id: "sim", text: "Sim", style: "primary" }, { id: "nao", text: "Não", style: "default" }], close_on_tap: true };
  }

  class Editor {
    constructor(api, toast) {
      this.api = api;
      this.toast = toast;
      this.layout = blank();
      this.name = "";
      this.saved = [];
      this.dragFrom = null;
      this.bind();
      this.render();
    }

    bind() {
      $("layout-id").addEventListener("input", (e) => { this.layout.id = clip(e.target.value, LIMITS.id); this.render(false); });
      $("layout-name").addEventListener("input", (e) => { this.name = e.target.value; });
      $("layout-close").addEventListener("change", (e) => { this.layout.close_on_tap = e.target.checked; this.render(false); });
      $("layout-timeout").addEventListener("input", (e) => {
        const v = parseInt(e.target.value, 10);
        if (v > 0) this.layout.timeout_s = Math.min(600, v); else delete this.layout.timeout_s;
        this.render(false);
      });
      $("layout-json").addEventListener("input", (e) => {
        try {
          const parsed = JSON.parse(e.target.value);
          if (parsed && parsed.v === 1) { this.layout = parsed; this.render(true, false); $("layout-json").style.borderColor = ""; }
        } catch { $("layout-json").style.borderColor = "var(--err)"; }
      });
      $("btn-new-layout").addEventListener("click", () => { this.layout = blank(); this.name = ""; this.render(); });
      $("btn-copy-layout").addEventListener("click", async () => {
        await navigator.clipboard.writeText(JSON.stringify(this.clean()));
        this.toast("JSON copiado (compacto, pronto para o layout do self.screen.render)");
      });
      $("btn-download-layout").addEventListener("click", () => {
        const blob = new Blob([JSON.stringify(this.clean(), null, 2)], { type: "application/json" });
        const a = Object.assign(document.createElement("a"), { href: URL.createObjectURL(blob), download: `${this.name || this.layout.id}.json` });
        a.click();
        URL.revokeObjectURL(a.href);
      });
      $("btn-save-layout").addEventListener("click", () => this.save());

      document.querySelectorAll("#palette .chip").forEach((chip) => {
        chip.addEventListener("dragstart", (e) => e.dataTransfer.setData("text/orb-kind", chip.dataset.kind));
        chip.addEventListener("click", () => this.add(chip.dataset.kind));
      });
      for (const target of [$("layout-preview"), $("element-list")]) {
        target.addEventListener("dragover", (e) => {
          if (e.dataTransfer.types.includes("text/orb-kind")) { e.preventDefault(); $("layout-preview").classList.add("drop-target"); }
        });
        target.addEventListener("dragleave", () => $("layout-preview").classList.remove("drop-target"));
        target.addEventListener("drop", (e) => {
          $("layout-preview").classList.remove("drop-target");
          const kind = e.dataTransfer.getData("text/orb-kind");
          if (kind) { e.preventDefault(); this.add(kind); }
        });
      }
    }

    add(kind) {
      const L = this.layout;
      if (kind === "title") { if (L.title) return this.toast("A tela já tem título"); L.title = "Título"; }
      if (kind === "body") { if (L.body) return this.toast("A tela já tem texto"); L.body = "Texto da tela"; }
      if (kind === "progress") { if (typeof L.progress === "number") return this.toast("A tela já tem progresso"); L.progress = 50; }
      if (kind === "timer") { if (L.timer) return this.toast("A tela já tem timer"); L.timer = { seconds: 60 }; }
      if (kind === "item") {
        L.items = L.items || [];
        if (L.items.length >= LIMITS.items) return this.toast(`No máximo ${LIMITS.items} caixas`);
        L.items.push({ id: `item${L.items.length + 1}`, text: "Nova tarefa", checked: false });
      }
      if (kind === "button") {
        L.buttons = L.buttons || [];
        if (L.buttons.length >= LIMITS.buttons) return this.toast(`No máximo ${LIMITS.buttons} botões`);
        L.buttons.push({ id: `botao${L.buttons.length + 1}`, text: "OK", style: "default" });
      }
      this.render();
    }

    // Layout sem campos vazios, como a placa espera
    clean() {
      const L = this.layout, out = { v: 1, id: clip(L.id, LIMITS.id) || "tela" };
      if (L.title) out.title = clip(L.title, LIMITS.title);
      if (L.body) out.body = clip(L.body, LIMITS.body);
      if (L.items?.length) out.items = L.items.slice(0, LIMITS.items).map((i) => ({ id: i.id, text: clip(i.text, LIMITS.label), checked: !!i.checked }));
      if (typeof L.progress === "number") out.progress = Math.max(0, Math.min(100, Math.round(L.progress)));
      if (L.timer) out.timer = { seconds: Math.max(1, Math.min(5999, Math.round(L.timer.seconds || 1))) };
      if (L.buttons?.length) out.buttons = L.buttons.slice(0, LIMITS.buttons).map((b) => ({ id: b.id, text: clip(b.text, LIMITS.label), style: b.style || "default" }));
      if (L.close_on_tap === false) out.close_on_tap = false;
      if (L.timeout_s > 0) out.timeout_s = Math.min(600, L.timeout_s);
      return out;
    }

    problems() {
      const L = this.clean(), msgs = [];
      if (!(L.title || L.body || L.items || "progress" in L || L.timer || L.buttons)) msgs.push("a tela precisa de pelo menos um elemento");
      const ids = [...(L.items || []), ...(L.buttons || [])].map((e) => e.id);
      if (ids.some((id) => !id)) msgs.push("toda caixa e botão precisa de id");
      if (new Set(ids).size !== ids.length) msgs.push("ids de caixas e botões repetidos");
      return msgs;
    }

    render(rebuildList = true, updateJson = true) {
      const L = this.layout;
      $("layout-id").value = L.id || "";
      $("layout-name").value = this.name;
      $("layout-close").checked = L.close_on_tap !== false;
      $("layout-timeout").value = L.timeout_s || "";
      if (rebuildList) this.renderElements();
      this.renderPreview();
      if (updateJson) $("layout-json").value = JSON.stringify(this.clean(), null, 2);
      const probs = this.problems();
      $("layout-caption").textContent = probs.length ? "⚠ " + probs.join("; ") : "Prévia aproximada (fonte e espaçamento podem variar um pouco na placa)";
      this.renderSaved();
    }

    renderPreview() {
      const L = this.clean(), parts = [];
      if (L.title) parts.push(`<div class="ls-title">${esc(L.title)}</div>`);
      if (L.body) parts.push(`<div class="ls-body">${esc(L.body)}</div>`);
      for (const it of L.items || []) parts.push(`<div class="ls-item${it.checked ? " checked" : ""}"><i></i>${esc(it.text)}</div>`);
      if ("progress" in L) parts.push(`<div class="ls-bar"><span style="width:${L.progress}%"></span></div>`);
      if (L.timer) { const s = L.timer.seconds; parts.push(`<div class="ls-timer">${String(Math.floor(s / 60)).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}</div>`); }
      if (L.buttons) parts.push(`<div class="ls-buttons">${L.buttons.map((b) => { const [bg, fg] = BUTTON_COLORS[b.style] || BUTTON_COLORS.default; return `<span style="background:${bg};color:${fg}">${esc(b.text)}</span>`; }).join("")}</div>`);
      $("layout-preview").innerHTML = parts.join("") || `<div class="ls-empty">Arraste elementos para cá</div>`;
    }

    field(label, value, onInput, attrs = "") {
      const wrap = document.createElement("label");
      wrap.className = "field";
      wrap.innerHTML = `${esc(label)}<input ${attrs}>`;
      const input = wrap.querySelector("input");
      if (input.type === "checkbox") { input.checked = !!value; input.addEventListener("change", () => { onInput(input.checked); this.render(false); }); }
      else { input.value = value ?? ""; input.addEventListener("input", () => { onInput(input.value); this.render(false); }); }
      return wrap;
    }

    card(kind, onRemove, list, index) {
      const el = document.createElement("div");
      el.className = "element";
      el.innerHTML = `<div class="element-head">${list ? '<span class="grip" title="Arraste para reordenar">⠿</span>' : ""}<b>${KIND_LABEL[kind]}</b><button class="remove" title="Remover">✕</button></div><div class="fields"></div>`;
      el.querySelector(".remove").addEventListener("click", () => { onRemove(); this.render(); });
      if (list) {
        el.draggable = true;
        el.addEventListener("dragstart", (e) => { this.dragFrom = { list, index }; e.dataTransfer.setData("text/orb-move", "1"); });
        el.addEventListener("dragover", (e) => { if (this.dragFrom?.list === list) { e.preventDefault(); el.classList.add("drag-over"); } });
        el.addEventListener("dragleave", () => el.classList.remove("drag-over"));
        el.addEventListener("drop", (e) => {
          el.classList.remove("drag-over");
          if (this.dragFrom?.list !== list) return;
          e.preventDefault(); e.stopPropagation();
          const arr = this.layout[list], [moved] = arr.splice(this.dragFrom.index, 1);
          arr.splice(index, 0, moved);
          this.dragFrom = null;
          this.render();
        });
      }
      $("element-list").appendChild(el);
      return el.querySelector(".fields");
    }

    renderElements() {
      const L = this.layout;
      $("element-list").innerHTML = "";
      if (L.title !== undefined) this.card("title", () => delete L.title).append(
        this.field("Texto", L.title, (v) => (L.title = v), `type="text" maxlength="${LIMITS.title}"`));
      if (L.body !== undefined) this.card("body", () => delete L.body).append(
        this.field("Texto", L.body, (v) => (L.body = v), `type="text" maxlength="${LIMITS.body}"`));
      (L.items || []).forEach((it, i) => this.card("item", () => L.items.splice(i, 1), "items", i).append(
        this.field("id", it.id, (v) => (it.id = v), `type="text" maxlength="${LIMITS.id}"`),
        this.field("Texto", it.text, (v) => (it.text = v), `type="text" maxlength="${LIMITS.label}"`),
        this.field("Marcada", it.checked, (v) => (it.checked = v), `type="checkbox"`)));
      if (typeof L.progress === "number") this.card("progress", () => delete L.progress).append(
        this.field("Valor (%)", L.progress, (v) => (L.progress = Number(v) || 0), `type="range" min="0" max="100"`));
      if (L.timer) this.card("timer", () => delete L.timer).append(
        this.field("Segundos", L.timer.seconds, (v) => (L.timer.seconds = Number(v) || 1), `type="number" min="1" max="5999"`));
      (L.buttons || []).forEach((b, i) => {
        const fields = this.card("button", () => L.buttons.splice(i, 1), "buttons", i);
        fields.append(
          this.field("id", b.id, (v) => (b.id = v), `type="text" maxlength="${LIMITS.id}"`),
          this.field("Texto", b.text, (v) => (b.text = v), `type="text" maxlength="${LIMITS.label}"`));
        const sel = document.createElement("label");
        sel.className = "field";
        sel.innerHTML = `Estilo<select><option value="primary">verde (primary)</option><option value="danger">vermelho (danger)</option><option value="default">roxo (default)</option></select>`;
        const s = sel.querySelector("select");
        s.value = b.style || "default";
        s.addEventListener("change", () => { b.style = s.value; this.render(false); });
        fields.append(sel);
      });
      if (!$("element-list").children.length) $("element-list").innerHTML = `<p class="hint">Nenhum elemento. Arraste da paleta ou clique num elemento.</p>`;
    }

    renderSaved() {
      const ul = $("saved-layouts");
      ul.innerHTML = "";
      if (!this.saved.length) ul.innerHTML = `<li class="hint" style="cursor:default">Nenhuma tela salva</li>`;
      for (const { name, layout } of this.saved) {
        const li = document.createElement("li");
        if (name === this.name) li.classList.add("current");
        li.innerHTML = `<span>${esc(name)}</span><button title="Apagar">✕</button>`;
        li.addEventListener("click", () => { this.layout = JSON.parse(JSON.stringify(layout)); this.name = name; this.render(); });
        li.querySelector("button").addEventListener("click", async (e) => {
          e.stopPropagation();
          if (!confirm(`Apagar a tela ${name}?`)) return;
          this.saved = (await this.api("/api/layouts/delete", { name })).layouts;
          this.render(false, false);
        });
        ul.appendChild(li);
      }
    }

    async load() {
      this.saved = (await this.api("/api/layouts")).layouts;
      this.renderSaved();
    }

    async save() {
      const probs = this.problems();
      if (probs.length) return this.toast(probs.join("; "), true);
      const name = (this.name || this.layout.id || "").trim();
      if (!name) return this.toast("Dê um nome ao arquivo", true);
      this.name = name;
      this.saved = (await this.api("/api/layouts", { name, layout: this.clean() })).layouts;
      this.render(false, false);
      this.toast(`Tela salva em studio/layouts/${name}.json`);
    }
  }

  return { Editor };
})();
