# screen_layout.cc e os headers display.h / mcp_server.h sao os do firmware.
# "stubs" vem antes de main/ para que "application.h" resolva para o simulador.
set(FW ${CMAKE_CURRENT_SOURCE_DIR}/../../../main)

idf_component_register(SRCS "main.cc" "stubs.cc" "${FW}/display/screen_layout.cc"
                       INCLUDE_DIRS "stubs" "${FW}/display" "${FW}"
                       REQUIRES esp_lcd esp_pm esp_timer esp_netif esp_event json nvs_flash
                                protocol_examples_common)
