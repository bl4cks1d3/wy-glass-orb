# Teste das telas interativas (main/display/screen_layout.cc) no QEMU:
# tela virtual RGB do QEMU + Ethernet emulada (open_eth) + protocolo Xiaozhi por WebSocket.
# Roteiro: probes/esp32_screen_layout_qemu.py na raiz do repositorio.
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(screen_layout_qemu)
