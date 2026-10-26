"""Arquivo .orb: a animacao de um orb, por estado, pronta para a placa tocar sem compilar.

    python studio/orbpack.py info  biblioteca/ameba.orb
    python studio/orbpack.py flash biblioteca/ameba.orb --port COM9

Formato (little-endian), especificado em docs/formato-orb.md:

    0   4  "ORB1"
    4   4  tamanho total do arquivo
    8   2  tamanho do cabecalho JSON (H)
    10  2  reservado (0)
    12  H  cabecalho JSON (UTF-8)
    ..  4  numero de quadros (N)
    ..  8N (offset, tamanho) de cada quadro, offset a partir do inicio do arquivo
    ..     quadros JPEG (baseline, sem progressivo)

Cabecalho: {"v": 1, "id", "name", "width": 192, "height": 192, "fps",
            "background": ["#RRGGBB" x3], "states": {"idle": [primeiro, quantidade], ...}}
"""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

MAGIC = b"ORB1"
SIZE = 192
STATES = ["starting", "idle", "connecting", "listening", "speaking", "error"]
PARTITION_OFFSET = 0xA00000  # particao "orbs" (partitions/v1/16m_orb.csv)
PARTITION_SIZE = 6 * 1024 * 1024


class OrbPackError(ValueError):
    pass


def build(meta: dict, frames: dict[str, list[bytes]]) -> bytes:
    """meta: id, name, fps, background (3 cores). frames: estado -> lista de JPEGs 192x192."""
    if "idle" not in frames or not frames["idle"]:
        raise OrbPackError("o estado idle (pronto) precisa de quadros")
    order, ranges = [], {}
    for state in STATES:
        if frames.get(state):
            ranges[state] = [len(order), len(frames[state])]
            order.extend(frames[state])
    for jpeg in order:
        if not jpeg.startswith(b"\xff\xd8"):
            raise OrbPackError("quadro que nao e JPEG")
    header = json.dumps({
        "v": 1,
        "id": meta["id"],
        "name": meta.get("name", meta["id"]),
        "width": SIZE,
        "height": SIZE,
        "fps": int(meta.get("fps", 15)),
        "background": meta.get("background", ["#071B2C", "#020711", "#010208"]),
        "states": ranges,
    }, ensure_ascii=False, separators=(",", ":")).encode()
    index_at = 12 + len(header)
    data_at = index_at + 4 + 8 * len(order)
    index, offset = [], data_at
    for jpeg in order:
        index.append(struct.pack("<II", offset, len(jpeg)))
        offset += len(jpeg)
    total = offset
    if total > PARTITION_SIZE:
        raise OrbPackError(f"orb com {total // 1024} KB passa dos {PARTITION_SIZE // 1024} KB da particao")
    return (MAGIC + struct.pack("<IHH", total, len(header), 0) + header + struct.pack("<I", len(order))
            + b"".join(index) + b"".join(order))


def read(data: bytes) -> tuple[dict, list[tuple[int, int]]]:
    """Confere o arquivo e devolve (cabecalho, [(offset, tamanho)])."""
    if data[:4] != MAGIC:
        raise OrbPackError("nao e um arquivo .orb")
    total, header_len, _ = struct.unpack_from("<IHH", data, 4)
    if total != len(data):
        raise OrbPackError(f"tamanho declarado {total} e diferente do arquivo ({len(data)})")
    header = json.loads(data[12:12 + header_len])
    pos = 12 + header_len
    (count,) = struct.unpack_from("<I", data, pos)
    frames = [struct.unpack_from("<II", data, pos + 4 + 8 * i) for i in range(count)]
    for off, size in frames:
        if off + size > total or data[off:off + 2] != b"\xff\xd8":
            raise OrbPackError("indice de quadros invalido")
    for state, (first, n) in header["states"].items():
        if first + n > count:
            raise OrbPackError(f"estado {state} aponta para quadros que nao existem")
    return header, frames


def flash_command(path: Path, port: str, baud: int = 921600) -> list[str]:
    return [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port, "--baud", str(baud),
            "write-flash", hex(PARTITION_OFFSET), str(path)]


def main():
    parser = argparse.ArgumentParser(description="Arquivos .orb")
    sub = parser.add_subparsers(dest="cmd", required=True)
    info = sub.add_parser("info", help="mostra o conteudo")
    info.add_argument("file", type=Path)
    flash = sub.add_parser("flash", help="grava na particao orbs da placa pela USB")
    flash.add_argument("file", type=Path)
    flash.add_argument("--port", required=True)
    args = parser.parse_args()
    data = args.file.read_bytes()
    header, frames = read(data)
    if args.cmd == "info":
        print(f"{header['name']} ({header['id']}), {header['width']}x{header['height']} a {header['fps']} qps, "
              f"{len(frames)} quadros, {len(data) // 1024} KB")
        for state, (first, n) in header["states"].items():
            print(f"  {state:11} {n:3} quadros ({n / header['fps']:.1f} s)")
    else:
        raise SystemExit(subprocess.call(flash_command(args.file, args.port)))


if __name__ == "__main__":
    main()
