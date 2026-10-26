#ifndef ORB_PACK_H
#define ORB_PACK_H

#include <esp_partition.h>

#include <cstdint>
#include <string>
#include <vector>

// Wy Glass: arquivo .orb gravado na particao "orbs" (docs/formato-orb.md). Sao quadros JPEG de
// 192x192 por estado, feitos no Orb Studio. Trocar de orb e gravar outro arquivo, sem compilar.
class OrbPack {
public:
    enum State { kStarting, kIdle, kConnecting, kListening, kSpeaking, kError, kStateCount };

    // Le e confere o cabecalho. False se a particao nao tem um orb valido.
    bool Load();
    bool loaded() const { return loaded_; }
    const std::string& name() const { return name_; }
    int fps() const { return fps_; }
    const uint32_t* background() const { return background_; }

    // Quadros de um estado; sem quadros proprios, usa os do idle
    void Range(State state, int& first, int& count) const;

    // Decodifica o quadro em out (RGB565, 192x192). Devolve o tempo em us, ou -1.
    int Decode(int index, uint16_t* out);

    static constexpr int kSize = 192;

private:
    const esp_partition_t* partition_ = nullptr;
    bool loaded_ = false;
    std::string name_;
    int fps_ = 15;
    uint32_t background_[3] = {0x071B2C, 0x020711, 0x010208};
    int first_[kStateCount] = {};
    int count_[kStateCount] = {};
    std::vector<uint32_t> offset_;
    std::vector<uint32_t> size_;
    uint8_t* jpeg_ = nullptr;  // quadro lido da flash (PSRAM)
    uint8_t* work_ = nullptr;  // area de trabalho do TJpgDec (SRAM interna, alocada uma vez)
};

#endif
