"""Orb Studio: ambiente visual para personalizar, compilar e gravar o firmware do orb.

    python studio/server.py            abre http://127.0.0.1:8740

So a biblioteca padrao do Python e o pyserial. O ESP-IDF precisa estar instalado: o Studio
procura em IDF_PATH ou em ~/esp/esp-idf e roda o export dele antes de cada comando.

A personalizacao fica em studio/orb.json. "Aplicar" gera main/orb_studio_config.h (aparencia e
textos do globo, rede do portal, servidor padrao) e atualiza a palavra de ativacao no sdkconfig.
As telas desenhadas ficam em studio/layouts/.
"""
import argparse
import json
import os
import platform
import re
import subprocess
import threading
import time
import unicodedata
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

STUDIO_DIR = Path(__file__).resolve().parent
PROJECT_DIR = STUDIO_DIR.parent
WEB_DIR = STUDIO_DIR / "web"
CONFIG_PATH = STUDIO_DIR / "orb.json"
LAYOUTS_DIR = STUDIO_DIR / "layouts"
HEADER_PATH = PROJECT_DIR / "main" / "orb_studio_config.h"
SDKCONFIG_PATH = PROJECT_DIR / "sdkconfig"

STATES = ["starting", "idle", "connecting", "listening", "speaking", "error"]

DEFAULTS = {
    "title": "WY GLASS ORB",
    "ap_prefix": "WyGlass",
    "default_server": "",
    "wake": {"enabled": True, "phrases": ["hey orb", "hi orb"], "threshold": 20},
    "globe": {
        "meridians": 12,
        "dots": 50,
        "base_speed": 0.012,
        "line_color": "#00D2FF",
        "halo_color": "#00BFFF",
        "label_color": "#48DFFF",
        "background": ["#071B2C", "#020711", "#010208"],
        "sphere": ["#0A2940", "#041522", "#01060D"],
    },
    "states": {
        "starting": {"color": "#00D2FF", "speed": 3.0, "brightness": 1.0, "pulse": 0.25, "period_ms": 1500,
                     "text": "CONECTANDO WI-FI", "text_alt": "WI-FI OK"},
        "idle": {"color": "#00D2FF", "speed": 1.0, "brightness": 1.0, "pulse": 0.08, "period_ms": 4000,
                 "text": "ONLINE", "text_alt": "SEM SERVIDOR"},
        "connecting": {"color": "#00D2FF", "speed": 4.0, "brightness": 1.0, "pulse": 0.2, "period_ms": 1200,
                       "text": "CONECTANDO"},
        "listening": {"color": "#00D2FF", "speed": 1.5, "brightness": 1.3, "pulse": 0.3, "period_ms": 900,
                      "text": "OUVINDO"},
        "speaking": {"color": "#00D2FF", "speed": 2.0, "brightness": 1.3, "pulse": 0.5, "period_ms": 500,
                     "text": "FALANDO"},
        "error": {"color": "#EF4444", "speed": 0.3, "brightness": 1.0, "pulse": 0.0, "period_ms": 1000,
                  "text": "ERRO"},
    },
    "offline_text": "SERVIDOR OFFLINE",
}


class ConfigError(ValueError):
    pass


# ---------------------------------------------------------------- configuracao

def ascii_text(value, limit: int, field: str) -> str:
    """A fonte do globo (Montserrat) so tem ASCII: tira acentos e corta no limite."""
    if not isinstance(value, str):
        raise ConfigError(f"{field}: texto esperado")
    text = unicodedata.normalize("NFKD", value).encode("ascii", "ignore").decode()
    text = re.sub(r"[\x00-\x1f\"\\]", "", text).strip()
    return text[:limit]


def color(value, field: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"#[0-9A-Fa-f]{6}", value):
        raise ConfigError(f"{field}: cor no formato #RRGGBB")
    return value.upper()


def number(value, lo: float, hi: float, field: str, integer: bool = False):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ConfigError(f"{field}: numero esperado")
    if not lo <= value <= hi:
        raise ConfigError(f"{field}: entre {lo} e {hi}")
    return int(value) if integer else float(value)


def validate(cfg: dict) -> dict:
    """Confere e normaliza a configuracao vinda da pagina. Campos ausentes usam o padrao."""
    merged = json.loads(json.dumps(DEFAULTS))
    for key in ("title", "ap_prefix", "default_server", "offline_text"):
        if key in cfg:
            merged[key] = cfg[key]
    for key in ("wake", "globe"):
        merged[key].update(cfg.get(key) or {})
    for state in STATES:
        merged["states"][state].update((cfg.get("states") or {}).get(state) or {})

    out = {
        "title": ascii_text(merged["title"], 24, "titulo") or DEFAULTS["title"],
        "ap_prefix": ascii_text(merged["ap_prefix"], 20, "rede do portal"),
        "default_server": merged["default_server"].strip() if isinstance(merged["default_server"], str) else "",
        "offline_text": ascii_text(merged["offline_text"], 24, "texto offline"),
    }
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,20}", out["ap_prefix"]):
        raise ConfigError("rede do portal: letras, numeros, _ e -, ate 20")
    if out["default_server"] and not re.fullmatch(r"wss?://[^\s\"\\]+", out["default_server"]):
        raise ConfigError("servidor padrao: vazio ou ws://... / wss://...")

    wake = merged["wake"]
    phrases = [p.strip().lower() for p in wake.get("phrases", []) if isinstance(p, str) and p.strip()]
    for p in phrases:
        if not re.fullmatch(r"[a-z]+( [a-z]+){0,5}", p):
            raise ConfigError(f"frase de ativacao '{p}': so palavras em ingles, letras minusculas")
    if wake.get("enabled") and not phrases:
        raise ConfigError("ative ao menos uma frase de ativacao ou desligue a ativacao por voz")
    out["wake"] = {
        "enabled": bool(wake.get("enabled")),
        "phrases": phrases[:8],
        "threshold": number(wake.get("threshold"), 1, 99, "limiar", integer=True),
    }

    g = merged["globe"]
    out["globe"] = {
        "meridians": number(g["meridians"], 2, 24, "meridianos", integer=True),
        "dots": number(g["dots"], 0, 120, "pontos", integer=True),
        "base_speed": number(g["base_speed"], 0.0, 0.1, "velocidade base"),
        "line_color": color(g["line_color"], "cor das linhas"),
        "halo_color": color(g["halo_color"], "cor do brilho"),
        "label_color": color(g["label_color"], "cor do texto"),
        "background": [color(c, "fundo") for c in g["background"]][:3],
        "sphere": [color(c, "esfera") for c in g["sphere"]][:3],
    }
    if len(out["globe"]["background"]) != 3 or len(out["globe"]["sphere"]) != 3:
        raise ConfigError("fundo e esfera precisam de 3 cores")

    out["states"] = {}
    for state in STATES:
        s = merged["states"][state]
        style = {
            "color": color(s["color"], f"{state}: cor"),
            "speed": number(s["speed"], 0.0, 10.0, f"{state}: velocidade"),
            "brightness": number(s["brightness"], 0.1, 3.0, f"{state}: brilho"),
            "pulse": number(s["pulse"], 0.0, 1.0, f"{state}: pulso"),
            "period_ms": number(s["period_ms"], 100, 10000, f"{state}: periodo", integer=True),
            "text": ascii_text(s["text"], 24, f"{state}: texto"),
        }
        if state in ("starting", "idle"):
            style["text_alt"] = ascii_text(s.get("text_alt", ""), 24, f"{state}: texto alternativo")
        out["states"][state] = style
    return out


def load_config() -> dict:
    if CONFIG_PATH.exists():
        return validate(json.loads(CONFIG_PATH.read_text(encoding="utf-8")))
    return validate({})


def save_config(cfg: dict) -> dict:
    cfg = validate(cfg)
    CONFIG_PATH.write_text(json.dumps(cfg, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return cfg


# ---------------------------------------------------------------- geracao

def c_str(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_hex(value: str) -> str:
    return "0x" + value[1:].upper()


def c_float(value: float) -> str:
    return f"{value:g}" + ("" if "." in f"{value:g}" or "e" in f"{value:g}" else ".0") + "f"


def render_header(cfg: dict) -> str:
    g, st = cfg["globe"], cfg["states"]

    def style(name: str, const: str) -> str:
        s = st[name]
        return (f"constexpr StateStyle {const} = {{{c_hex(s['color'])}, {c_float(s['speed'])}, "
                f"{c_float(s['brightness'])}, {c_float(s['pulse'])}, {s['period_ms']}, {c_str(s['text'])}}};")

    lines = [
        "// Gerado pelo Orb Studio (studio/) a partir de studio/orb.json. Edite pelo Studio:",
        "// mudancas feitas aqui a mao sao sobrescritas na proxima vez que ele aplicar.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "",
        "namespace orb_studio {",
        "",
        "struct StateStyle {",
        "    uint32_t color;      // cor das linhas e pontos do globo",
        "    float speed;         // multiplica a rotacao base",
        "    float brightness;    // brilho das linhas",
        "    float pulse;         // amplitude do pulso (0 = sem pulso)",
        "    uint32_t period_ms;  // periodo do pulso",
        "    const char* text;    // texto embaixo do globo (sem acentos: fonte Montserrat)",
        "};",
        "",
        f"constexpr const char* kTitle = {c_str(cfg['title'])};",
        f"constexpr const char* kApPrefix = {c_str(cfg['ap_prefix'])};",
        f"constexpr const char* kDefaultServer = {c_str(cfg['default_server'])};",
        "",
        f"constexpr int kMeridians = {g['meridians']};",
        f"constexpr int kDots = {g['dots']};",
        f"constexpr float kBaseSpeed = {c_float(g['base_speed'])};",
        "",
        f"constexpr uint32_t kLineColor = {c_hex(g['line_color'])};",
        f"constexpr uint32_t kHaloColor = {c_hex(g['halo_color'])};",
        f"constexpr uint32_t kLabelColor = {c_hex(g['label_color'])};",
        f"constexpr uint32_t kBackground[3] = {{{', '.join(c_hex(c) for c in g['background'])}}};",
        f"constexpr uint32_t kSphere[3] = {{{', '.join(c_hex(c) for c in g['sphere'])}}};",
        "",
        style("starting", "kStarting"),
        f"constexpr const char* kStartingReadyText = {c_str(st['starting']['text_alt'])};",
        style("idle", "kIdle"),
        f"constexpr const char* kIdleNoServerText = {c_str(st['idle']['text_alt'])};",
        style("connecting", "kConnecting"),
        style("listening", "kListening"),
        style("speaking", "kSpeaking"),
        style("error", "kError"),
        f"constexpr const char* kOfflineText = {c_str(cfg['offline_text'])};",
        "",
        "}  // namespace orb_studio",
        "",
    ]
    return "\n".join(lines)


def update_sdkconfig(text: str, wake: dict) -> str:
    """Liga/desliga a palavra de ativacao e grava frases e limiar no sdkconfig."""
    lines = [ln for ln in text.splitlines()
             if not re.match(r"(# )?CONFIG_(USE_ORB_WAKE_WORD|ORB_WAKE_WORD|ORB_WAKE_WORD_THRESHOLD)[= ]", ln)]
    block = ["CONFIG_USE_ORB_WAKE_WORD=y",
             f'CONFIG_ORB_WAKE_WORD="{";".join(wake["phrases"])}"',
             f"CONFIG_ORB_WAKE_WORD_THRESHOLD={wake['threshold']}"] if wake["enabled"] else \
        ["# CONFIG_USE_ORB_WAKE_WORD is not set"]
    anchor = next((i for i, ln in enumerate(lines) if "CONFIG_USE_AFE_WAKE_WORD" in ln), len(lines) - 1)
    lines[anchor + 1:anchor + 1] = block
    return "\n".join(lines) + "\n"


def apply_config(cfg: dict) -> list[str]:
    cfg = save_config(cfg)
    changed = []
    header = render_header(cfg)
    if not HEADER_PATH.exists() or HEADER_PATH.read_text(encoding="utf-8") != header:
        HEADER_PATH.write_text(header, encoding="utf-8", newline="\n")
        changed.append(str(HEADER_PATH.relative_to(PROJECT_DIR)))
    sdk = SDKCONFIG_PATH.read_text(encoding="utf-8")
    new_sdk = update_sdkconfig(sdk, cfg["wake"])
    if new_sdk != sdk:
        SDKCONFIG_PATH.write_text(new_sdk, encoding="utf-8", newline="\n")
        changed.append("sdkconfig")
    return changed


# ---------------------------------------------------------------- telas

LAYOUT_NAME = re.compile(r"[A-Za-z0-9_-]{1,40}")


def list_layouts() -> list[dict]:
    LAYOUTS_DIR.mkdir(exist_ok=True)
    out = []
    for path in sorted(LAYOUTS_DIR.glob("*.json")):
        try:
            out.append({"name": path.stem, "layout": json.loads(path.read_text(encoding="utf-8"))})
        except json.JSONDecodeError:
            continue
    return out


def save_layout(name: str, layout: dict) -> None:
    if not LAYOUT_NAME.fullmatch(name or ""):
        raise ConfigError("nome da tela: letras, numeros, _ e -, ate 40")
    if not isinstance(layout, dict) or layout.get("v") != 1 or not layout.get("id"):
        raise ConfigError("layout precisa de v: 1 e id")
    LAYOUTS_DIR.mkdir(exist_ok=True)
    (LAYOUTS_DIR / f"{name}.json").write_text(json.dumps(layout, ensure_ascii=False, indent=2) + "\n",
                                              encoding="utf-8")


def delete_layout(name: str) -> None:
    if LAYOUT_NAME.fullmatch(name or ""):
        (LAYOUTS_DIR / f"{name}.json").unlink(missing_ok=True)


# ---------------------------------------------------------------- tarefas (build, gravacao, serial)

class Console:
    """Log compartilhado das tarefas, lido pela pagina via Server-Sent Events."""

    def __init__(self, limit: int = 4000):
        self.lines: list[tuple[int, str, str]] = []  # (seq, origem, texto)
        self.seq = 0
        self.limit = limit
        self.cond = threading.Condition()
        self.status = {"job": None, "running": False, "result": None, "monitor": None}

    def write(self, source: str, text: str):
        with self.cond:
            for line in text.splitlines() or [""]:
                self.seq += 1
                self.lines.append((self.seq, source, line))
            del self.lines[:-self.limit]
            self.cond.notify_all()

    def set_status(self, **kw):
        with self.cond:
            self.status.update(kw)
            self.seq += 1
            self.lines.append((self.seq, "status", json.dumps(self.status)))
            self.cond.notify_all()

    def since(self, seq: int, timeout: float = 15.0):
        with self.cond:
            if not any(s > seq for s, _, _ in self.lines[-1:]):
                self.cond.wait(timeout)
            return [ln for ln in self.lines if ln[0] > seq]


console = Console()
job_lock = threading.Lock()
current_proc: subprocess.Popen | None = None
monitor_stop = threading.Event()
monitor_thread: threading.Thread | None = None


def idf_path() -> Path | None:
    for candidate in (os.environ.get("IDF_PATH"), Path.home() / "esp" / "esp-idf"):
        if candidate and (Path(candidate) / "tools" / "idf.py").exists():
            return Path(candidate)
    return None


def idf_command(args: str) -> list[str]:
    idf = idf_path()
    if idf is None:
        raise ConfigError("ESP-IDF nao encontrado: defina IDF_PATH ou instale em ~/esp/esp-idf")
    if platform.system() == "Windows":
        script = f". '{idf / 'export.ps1'}' *> $null; idf.py {args}; exit $LASTEXITCODE"
        return ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script]
    return ["bash", "-c", f"source '{idf / 'export.sh'}' >/dev/null 2>&1 && idf.py {args}"]


def run_job(name: str, steps: list[str]):
    global current_proc
    console.set_status(job=name, running=True, result=None)
    ok = True
    try:
        for args in steps:
            console.write("job", f"$ idf.py {args}")
            env = dict(os.environ, PYTHONUNBUFFERED="1", IDF_COMPONENT_CHECK_NEW_VERSION="0")
            current_proc = subprocess.Popen(idf_command(args), cwd=PROJECT_DIR, env=env, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, bufsize=1, text=True, encoding="utf-8",
                                            errors="replace")
            for line in current_proc.stdout:
                console.write("job", line.rstrip("\r\n"))
            if current_proc.wait() != 0:
                ok = False
                break
    except Exception as e:  # noqa: BLE001 -- o erro vai para o log da pagina
        console.write("job", f"ERRO: {e}")
        ok = False
    finally:
        current_proc = None
        console.set_status(running=False, result="ok" if ok else "falhou")
        job_lock.release()


def start_job(action: str, port: str, baud: int = 921600):
    steps = {
        "build": ["build"],
        "flash": [f"-p {port} -b {baud} flash"],
        "build_flash": ["build", f"-p {port} -b {baud} flash"],
        "fullclean": ["fullclean"],
    }.get(action)
    if steps is None:
        raise ConfigError("acao desconhecida")
    if action in ("flash", "build_flash"):
        if not re.fullmatch(r"(COM\d+|/dev/[\w./-]+)", port or ""):
            raise ConfigError("escolha a porta da placa")
        stop_monitor()  # a gravacao precisa da porta
    if not job_lock.acquire(blocking=False):
        raise ConfigError("ja existe uma tarefa rodando")
    threading.Thread(target=run_job, args=(action, steps), daemon=True).start()


def stop_job():
    proc = current_proc
    if proc and proc.poll() is None:
        if platform.system() == "Windows":
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], capture_output=True)
        else:
            proc.kill()
        console.write("job", "Tarefa interrompida.")


def list_ports() -> list[dict]:
    from serial.tools import list_ports as lp
    ports = []
    for p in lp.comports():
        orb = "303A" in (p.hwid or "").upper()
        ports.append({"device": p.device, "description": p.description, "orb": orb})
    ports.sort(key=lambda p: not p["orb"])
    return ports


def monitor_loop(port: str):
    import serial
    console.set_status(monitor=port)
    console.write("serial", f"--- monitor serial em {port} (115200)")
    buf = b""
    while not monitor_stop.is_set():
        try:
            with serial.Serial(port, 115200, timeout=0.2) as ser:
                while not monitor_stop.is_set():
                    buf += ser.read(4096)
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = re.sub(r"\x1b\[[0-9;]*m", "", line.decode("utf-8", "replace")).rstrip("\r")
                        console.write("serial", text)
        except Exception:  # noqa: BLE001 -- a placa reinicia e a porta USB some por um instante
            if monitor_stop.wait(0.5):
                break
    console.write("serial", "--- monitor parado")
    console.set_status(monitor=None)


def start_monitor(port: str, reset: bool = False):
    global monitor_thread
    if not re.fullmatch(r"(COM\d+|/dev/[\w./-]+)", port or ""):
        raise ConfigError("escolha a porta da placa")
    if job_lock.locked():
        raise ConfigError("espere a tarefa terminar")
    stop_monitor()
    if reset:
        import serial
        with serial.Serial(port, 115200) as ser:  # reinicia a placa pela USB (RTS)
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
        time.sleep(0.3)
    monitor_stop.clear()
    monitor_thread = threading.Thread(target=monitor_loop, args=(port,), daemon=True)
    monitor_thread.start()


def stop_monitor():
    global monitor_thread
    if monitor_thread and monitor_thread.is_alive():
        monitor_stop.set()
        monitor_thread.join(timeout=3)
    monitor_thread = None


# ---------------------------------------------------------------- HTTP

CONTENT_TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8",
                 ".css": "text/css; charset=utf-8", ".svg": "image/svg+xml", ".png": "image/png"}


class Handler(BaseHTTPRequestHandler):
    server_version = "OrbStudio"

    def log_message(self, fmt, *args):  # silencioso: o log util e o das tarefas
        pass

    def send_json(self, data, status=HTTPStatus.OK):
        body = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def read_json(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        return json.loads(self.rfile.read(length) or b"{}")

    def local_only(self) -> bool:
        # A API mexe em arquivos e na placa: so aceita a propria pagina (sem outro Origin/Host)
        host = (self.headers.get("Host") or "").split(":")[0]
        origin = self.headers.get("Origin")
        if host not in ("127.0.0.1", "localhost") or (origin and not re.match(r"https?://(127\.0\.0\.1|localhost)(:\d+)?$", origin)):
            self.send_json({"error": "acesso so pelo proprio computador"}, HTTPStatus.FORBIDDEN)
            return False
        return True

    def do_GET(self):
        if not self.local_only():
            return
        path = self.path.split("?")[0]
        try:
            if path == "/api/config":
                return self.send_json({"config": load_config(), "defaults": validate({})})
            if path == "/api/ports":
                return self.send_json({"ports": list_ports()})
            if path == "/api/layouts":
                return self.send_json({"layouts": list_layouts()})
            if path == "/api/status":
                return self.send_json({**console.status, "idf": str(idf_path() or ""), "project": str(PROJECT_DIR)})
            if path == "/api/console":
                return self.stream_console()
        except ConfigError as e:
            return self.send_json({"error": str(e)}, HTTPStatus.BAD_REQUEST)
        self.serve_static(path)

    def do_POST(self):
        if not self.local_only():
            return
        path = self.path.split("?")[0]
        try:
            body = self.read_json()
            if path == "/api/config":
                return self.send_json({"config": save_config(body.get("config", {}))})
            if path == "/api/apply":
                changed = apply_config(body.get("config", {}))
                console.write("job", "Personalizacao aplicada: " + (", ".join(changed) or "nada mudou"))
                return self.send_json({"changed": changed})
            if path == "/api/job":
                if body.get("apply_first"):
                    apply_config(body.get("config") or load_config())
                start_job(body.get("action"), body.get("port", ""))
                return self.send_json({"ok": True})
            if path == "/api/job/stop":
                stop_job()
                return self.send_json({"ok": True})
            if path == "/api/monitor":
                if body.get("on"):
                    start_monitor(body.get("port", ""), bool(body.get("reset")))
                else:
                    stop_monitor()
                return self.send_json({"ok": True})
            if path == "/api/layouts":
                save_layout(body.get("name", ""), body.get("layout"))
                return self.send_json({"layouts": list_layouts()})
            if path == "/api/layouts/delete":
                delete_layout(body.get("name", ""))
                return self.send_json({"layouts": list_layouts()})
        except (ConfigError, json.JSONDecodeError) as e:
            return self.send_json({"error": str(e)}, HTTPStatus.BAD_REQUEST)
        self.send_json({"error": "rota desconhecida"}, HTTPStatus.NOT_FOUND)

    def stream_console(self):
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        last = max(0, console.seq - 400)  # ao conectar, manda o log recente
        try:
            self.wfile.write(f"event: status\ndata: {json.dumps(console.status)}\n\n".encode())
            while True:
                lines = console.since(last)
                if not lines:
                    self.wfile.write(b": ping\n\n")
                for seq, source, text in lines:
                    last = seq
                    event = "status" if source == "status" else "line"
                    data = text if event == "status" else json.dumps({"s": source, "t": text}, ensure_ascii=False)
                    self.wfile.write(f"event: {event}\ndata: {data}\n\n".encode())
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def serve_static(self, path: str):
        rel = "index.html" if path in ("", "/") else path.lstrip("/")
        target = (WEB_DIR / rel).resolve()
        if WEB_DIR not in target.parents or not target.is_file():
            return self.send_json({"error": "nao encontrado"}, HTTPStatus.NOT_FOUND)
        body = target.read_bytes()
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", CONTENT_TYPES.get(target.suffix, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)


def main():
    parser = argparse.ArgumentParser(description="Orb Studio")
    parser.add_argument("--port", type=int, default=8740)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.daemon_threads = True
    url = f"http://127.0.0.1:{args.port}"
    print(f"Orb Studio em {url}  (ESP-IDF: {idf_path() or 'nao encontrado'})")
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop_monitor()
        stop_job()


if __name__ == "__main__":
    main()
