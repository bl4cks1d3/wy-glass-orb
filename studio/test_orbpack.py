"""Testes do formato .orb (python -m pytest studio)."""
import struct

import orbpack
import pytest

JPEG = b"\xff\xd8\xff\xe0" + b"x" * 20 + b"\xff\xd9"


def test_ida_e_volta():
    data = orbpack.build({"id": "teste", "name": "Teste", "fps": 12},
                         {"idle": [JPEG, JPEG], "speaking": [JPEG], "nao_existe": [JPEG]})
    header, frames = orbpack.read(data)
    assert header["id"] == "teste" and header["fps"] == 12
    assert header["width"] == header["height"] == 192
    assert header["states"] == {"idle": [0, 2], "speaking": [2, 1]}
    assert len(frames) == 3
    assert all(data[off:off + size] == JPEG for off, size in frames)


def test_idle_obrigatorio():
    with pytest.raises(orbpack.OrbPackError):
        orbpack.build({"id": "x"}, {"speaking": [JPEG]})


def test_recusa_quadro_que_nao_e_jpeg():
    with pytest.raises(orbpack.OrbPackError):
        orbpack.build({"id": "x"}, {"idle": [b"PNG..."]})


def test_recusa_arquivo_cortado_ou_alheio():
    data = orbpack.build({"id": "x"}, {"idle": [JPEG]})
    with pytest.raises(orbpack.OrbPackError):
        orbpack.read(data[:-1])
    with pytest.raises(orbpack.OrbPackError):
        orbpack.read(b"XXXX" + data[4:])


def test_estado_apontando_para_fora():
    data = bytearray(orbpack.build({"id": "x"}, {"idle": [JPEG]}))
    header_len = struct.unpack_from("<H", data, 8)[0]
    header = data[12:12 + header_len].replace(b'"idle":[0,1]', b'"idle":[0,9]')
    with pytest.raises(orbpack.OrbPackError):
        orbpack.read(bytes(data[:12] + header + data[12 + header_len:]))


def test_comando_de_gravacao_usa_a_particao_orbs():
    cmd = orbpack.flash_command("a.orb", "COM9")
    assert "write-flash" in cmd and hex(orbpack.PARTITION_OFFSET) in cmd and "COM9" in cmd
