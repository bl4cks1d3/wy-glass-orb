#ifndef ORB_SYNC_H
#define ORB_SYNC_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string>

// Wy Glass: troca de orb pelo Wi-Fi. Pergunta ao servidor (orb_gateway, GET /orbs/current) qual
// orb mostrar; se o sha256 for outro, baixa o .orb para a particao orbs e troca na hora, sem
// reiniciar. Confere no boot, a cada minuto em "pronto" e quando o servidor manda
// self.orb.update (troca por voz). Formato: docs/formato-orb.md.
class OrbSync {
public:
    static OrbSync& GetInstance() {
        static OrbSync instance;
        return instance;
    }

    // ws_url: endereco do servidor (ws://host:porta/caminho); as rotas /orbs ficam no mesmo host
    void SetServer(const std::string& ws_url);
    // Cria a tarefa. So depois do boot: a pilha fica na SRAM interna, que no boot chega ao limite
    // enquanto os modelos de voz carregam (criar antes derrubava o radio por falta de memoria)
    void Start();
    // Confere agora, sem esperar o proximo minuto
    void CheckNow();
    static void RegisterMcpTool();

private:
    OrbSync() = default;
    std::string base_;  // http://host:porta
    TaskHandle_t task_ = nullptr;

    void Loop();
    void Check();
    bool Download(const std::string& id, size_t size, const std::string& sha256);
};

#endif
