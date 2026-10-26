// Orb Studio: personalizar (com pre-visualizacao), telas e compilar/gravar.
"use strict";

const $ = (id) => document.getElementById(id);
const STATE_LABEL = { starting: "Iniciando", idle: "Pronto", connecting: "Conectando", listening: "Ouvindo", speaking: "Falando", error: "Erro" };

let config = null;
let defaults = null;
let dirty = false;
let editingState = "idle";
let toastTimer = null;

function toast(message, error = false) {
  const el = $("toast");
  el.textContent = message;
  el.classList.toggle("error", error);
  el.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove("show"), error ? 6000 : 3000);
}

async function api(path, body) {
  const res = await fetch(path, body === undefined ? {} : {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body),
  });
  const data = await res.json();
  if (!res.ok) { toast(data.error || `erro ${res.status}`, true); throw new Error(data.error); }
  return data;
}

// ---------------------------------------------------------------- abas

document.querySelectorAll("nav button").forEach((btn) => btn.addEventListener("click", () => {
  document.querySelectorAll("nav button").forEach((b) => b.setAttribute("aria-selected", b === btn));
  document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("active", t.id === `tab-${btn.dataset.tab}`));
  if (btn.dataset.tab === "build") refreshPorts();
}));

// ---------------------------------------------------------------- personalizar

const preview = new OrbPreview.Preview($("preview"));
const minis = [];

// Galeria de tipos: uma miniatura animada por tipo (mais lenta e sem textos)
function renderGallery() {
  const box = $("style-gallery");
  box.innerHTML = "";
  minis.length = 0;
  for (const [id, def] of Object.entries(OrbPreview.STYLES)) {
    const card = document.createElement("button");
    card.className = "style-card";
    card.type = "button";
    card.dataset.style = id;
    card.setAttribute("aria-pressed", config.style === id);
    card.innerHTML = `<canvas width="240" height="240"></canvas><b>${def.name}</b><span>${def.description}</span>`;
    card.addEventListener("click", () => selectStyle(id));
    box.appendChild(card);
    const mini = new OrbPreview.Preview(card.querySelector("canvas"), { style: id, tickMs: 100, overlay: false });
    mini.setConfig(config);
    minis.push(mini);
  }
}

function selectStyle(id) {
  config.style = id;
  document.querySelectorAll(".style-card").forEach((c) => c.setAttribute("aria-pressed", c.dataset.style === id));
  preview.refreshStyle();
  markDirty();
}

const getPath = (obj, path) => path.split(".").reduce((o, k) => o?.[k], obj);
function setPath(obj, path, value) {
  const keys = path.split(".");
  const last = keys.pop();
  keys.reduce((o, k) => o[k], obj)[last] = value;
}

function markDirty() {
  dirty = true;
  $("dirty").hidden = false;
}

function bindInput(input) {
  const path = input.dataset.path;
  const out = input.parentElement.querySelector("output");
  const show = () => { if (out) out.textContent = input.step && input.step < 1 ? Number(input.value).toFixed(3) : input.value; };
  input.addEventListener("input", () => {
    let v = input.type === "checkbox" ? input.checked : input.value;
    if (input.type === "range" || input.type === "number") v = Number(v);
    if (input.type === "color") v = v.toUpperCase();
    setPath(config, path, v);
    show();
    markDirty();
    if (path.startsWith("globe.")) { preview.setConfig(config); minis.forEach((m) => m.setConfig(config)); }
  });
  input._sync = () => {
    const v = getPath(config, path);
    if (input.type === "checkbox") input.checked = !!v; else input.value = v ?? "";
    show();
  };
}

function syncInputs() {
  document.querySelectorAll("#tab-customize [data-path]").forEach((i) => i._sync?.());
  $("wake-phrases").value = config.wake.phrases.join(", ");
}

$("wake-phrases").addEventListener("input", (e) => {
  config.wake.phrases = e.target.value.split(",").map((p) => p.trim().toLowerCase()).filter(Boolean);
  markDirty();
});

function renderStateTabs() {
  for (const id of ["state-tabs", "preview-states"]) {
    const bar = $(id);
    bar.innerHTML = "";
    for (const st of OrbPreview.STATES) {
      const b = document.createElement("button");
      b.textContent = STATE_LABEL[st];
      b.setAttribute("aria-pressed", st === editingState);
      b.addEventListener("click", () => selectState(st));
      bar.appendChild(b);
    }
  }
}

function selectState(st) {
  editingState = st;
  preview.setState(st);
  preview.setScreen("globe");
  document.querySelectorAll("#screen-tabs button").forEach((b) => b.setAttribute("aria-pressed", b.dataset.screen === "globe"));
  renderStateTabs();
  renderStateFields();
}

function renderStateFields() {
  const st = editingState, box = $("state-fields");
  const p = `states.${st}`;
  box.innerHTML = `
    <label class="field">Cor das linhas e pontos<input type="color" data-path="${p}.color"></label>
    <label class="field">Velocidade<span class="range-row"><input type="range" min="0" max="10" step="0.1" data-path="${p}.speed"><output></output></span></label>
    <label class="field">Brilho<span class="range-row"><input type="range" min="0.1" max="3" step="0.05" data-path="${p}.brightness"><output></output></span></label>
    <label class="field">Pulso<span class="range-row"><input type="range" min="0" max="1" step="0.01" data-path="${p}.pulse"><output></output></span></label>
    <label class="field">Período do pulso (ms)<span class="range-row"><input type="range" min="100" max="10000" step="50" data-path="${p}.period_ms"><output></output></span></label>
    <label class="field">Texto<input type="text" maxlength="24" data-path="${p}.text"></label>
    ${st === "starting" ? `<label class="field">Texto com Wi-Fi conectado<input type="text" maxlength="24" data-path="${p}.text_alt"></label>` : ""}
    ${st === "idle" ? `<label class="field">Texto sem servidor<input type="text" maxlength="24" data-path="${p}.text_alt"></label>` : ""}`;
  box.querySelectorAll("[data-path]").forEach((i) => { bindInput(i); i._sync(); });
}

document.querySelectorAll("#screen-tabs button").forEach((b) => b.addEventListener("click", () => {
  document.querySelectorAll("#screen-tabs button").forEach((x) => x.setAttribute("aria-pressed", x === b));
  preview.setScreen(b.dataset.screen);
}));

$("btn-save").addEventListener("click", async () => {
  config = (await api("/api/config", { config })).config;
  preview.setConfig(config);
  renderGallery();
  syncInputs();
  toast("Salvo em studio/orb.json (ainda não aplicado no firmware)");
});

$("btn-apply").addEventListener("click", async () => {
  const { changed } = await api("/api/apply", { config });
  dirty = false;
  $("dirty").hidden = true;
  toast(changed.length ? `Aplicado: ${changed.join(", ")}. Agora compile e grave.` : "O firmware já está com esta personalização.");
});

$("btn-reset").addEventListener("click", () => {
  if (!confirm("Voltar toda a personalização para o padrão?")) return;
  config = JSON.parse(JSON.stringify(defaults));
  preview.setConfig(config);
  renderGallery();
  syncInputs();
  renderStateFields();
  markDirty();
});

// ---------------------------------------------------------------- compilar e gravar

const consoleEl = $("console");
let consoleFilter = "all";
let lineCount = 0;

function addLine(source, text) {
  const div = document.createElement("div");
  div.className = source;
  if (/^E \(|error:|ERRO|FAILED|fatal/i.test(text)) div.classList.add("e");
  else if (/^W \(|warning:/i.test(text)) div.classList.add("w");
  else if (text.startsWith("$ ")) div.classList.add("cmd");
  div.textContent = text;
  div.hidden = consoleFilter !== "all" && consoleFilter !== source;
  consoleEl.appendChild(div);
  if (++lineCount > 5000) { consoleEl.firstChild.remove(); lineCount--; }
  if ($("autoscroll").checked) consoleEl.scrollTop = consoleEl.scrollHeight;
}

function applyStatus(s) {
  const chip = $("job-status");
  chip.className = "status-chip";
  const names = { build: "compilando", flash: "gravando", build_flash: "compilando e gravando", fullclean: "limpando" };
  if (s.running) { chip.classList.add("running"); chip.textContent = names[s.job] || s.job; }
  else if (s.result === "ok") { chip.classList.add("ok"); chip.textContent = `${names[s.job] || s.job}: concluído`; }
  else if (s.result) { chip.classList.add("fail"); chip.textContent = `${names[s.job] || s.job}: falhou`; }
  else chip.textContent = "pronto";
  if (s.monitor) chip.textContent += ` · monitor em ${s.monitor}`;
  document.querySelectorAll("[data-job]").forEach((b) => (b.disabled = !!s.running));
  $("btn-stop").disabled = !s.running;
  $("btn-monitor").textContent = s.monitor ? "Fechar monitor" : "Abrir monitor";
  $("btn-monitor").dataset.on = s.monitor ? "1" : "";
}

function connectConsole() {
  const es = new EventSource("/api/console");
  es.addEventListener("line", (e) => { const { s, t } = JSON.parse(e.data); addLine(s, t); });
  es.addEventListener("status", (e) => applyStatus(JSON.parse(e.data)));
  es.onerror = () => { es.close(); setTimeout(connectConsole, 2000); };
}

async function refreshPorts() {
  const { ports } = await api("/api/ports");
  const sel = $("port"), current = sel.value;
  sel.innerHTML = ports.map((p) => `<option value="${p.device}">${p.device}${p.orb ? " · orb (ESP32-S3)" : ""} — ${p.description}</option>`).join("")
    || `<option value="">nenhuma porta</option>`;
  const orb = ports.find((p) => p.orb);
  sel.value = ports.some((p) => p.device === current) ? current : orb?.device || ports[0]?.device || "";
  $("port-hint").textContent = orb ? "" : "A placa não apareceu. No Windows, confira se a interface serial está com o driver \"Dispositivo Serial USB\" (docs/primeiro-uso.md).";
}

$("btn-ports").addEventListener("click", refreshPorts);
document.querySelectorAll("[data-job]").forEach((btn) => btn.addEventListener("click", () => {
  if (btn.dataset.job === "fullclean" && !confirm("Apagar a pasta build? O próximo build leva alguns minutos.")) return;
  api("/api/job", { action: btn.dataset.job, port: $("port").value, apply_first: $("apply-first").checked, config })
    .then(() => { if ($("apply-first").checked) { dirty = false; $("dirty").hidden = true; } })
    .catch(() => {});
}));
$("btn-stop").addEventListener("click", () => api("/api/job/stop", {}));
$("btn-monitor").addEventListener("click", () => api("/api/monitor", { on: !$("btn-monitor").dataset.on, port: $("port").value }).catch(() => {}));
$("btn-monitor-reset").addEventListener("click", () => api("/api/monitor", { on: true, reset: true, port: $("port").value }).catch(() => {}));
$("btn-clear").addEventListener("click", () => { consoleEl.innerHTML = ""; lineCount = 0; });
document.querySelectorAll("#console-filter button").forEach((b) => b.addEventListener("click", () => {
  consoleFilter = b.dataset.filter;
  document.querySelectorAll("#console-filter button").forEach((x) => x.setAttribute("aria-pressed", x === b));
  consoleEl.querySelectorAll("div").forEach((d) => (d.hidden = consoleFilter !== "all" && !d.classList.contains(consoleFilter)));
}));

// ---------------------------------------------------------------- inicio

(async function start() {
  const data = await api("/api/config");
  config = data.config;
  defaults = data.defaults;
  preview.setConfig(config);
  renderGallery();
  document.querySelectorAll("#tab-customize [data-path]").forEach(bindInput);
  syncInputs();
  selectState("idle");
  const status = await api("/api/status");
  $("header-info").innerHTML = `projeto <code>${status.project}</code><br>ESP-IDF ${status.idf ? `<code>${status.idf}</code>` : "<b style='color:var(--err)'>não encontrado</b>"}`;
  new LayoutEditor.Editor(api, toast).load();
  connectConsole();
  window.addEventListener("beforeunload", (e) => { if (dirty) e.preventDefault(); });
})();
