#ifndef SCREEN_LAYOUT_H
#define SCREEN_LAYOUT_H

#include <lvgl.h>
#include <cJSON.h>
#include <atomic>
#include <string>
#include <vector>

class Display;

// Telas interativas declarativas (Wy Glass): o servidor manda um layout JSON
// pela ferramenta MCP self.screen.render e os toques voltam como
// notifications/ui_event. Esquema em docs/screen-layout.md.
class ScreenLayout {
public:
    static ScreenLayout& GetInstance() {
        static ScreenLayout instance;
        return instance;
    }

    // Desenha o layout por cima da interface. Retorna "" ou a mensagem de erro.
    std::string Show(Display* display, const std::string& json);
    void Close(Display* display);
    bool IsOpen() const { return open_; }

    // Registra self.screen.render e self.screen.close no McpServer.
    static void RegisterMcpTools(Display* display);

private:
    ScreenLayout() = default;

    struct Element {
        std::string id;
        bool close_on_tap;
    };

    lv_obj_t* root_ = nullptr;
    lv_obj_t* timer_label_ = nullptr;
    lv_timer_t* countdown_ = nullptr;
    lv_timer_t* timeout_ = nullptr;
    std::string layout_id_;
    std::vector<Element> elements_;
    int remaining_s_ = 0;
    uint32_t generation_ = 0;
    std::atomic<bool> open_{false};

    // Chamadas com o lock do LVGL já tomado.
    void Build(cJSON* layout);
    void Teardown(bool async);
    void* Tag(size_t index) const;
    bool Untag(void* user_data, size_t* index) const;
    void SendEvent(const std::string& element, const char* event, int checked = -1);
    void UpdateTimerLabel();

    static void OnButton(lv_event_t* e);
    static void OnItem(lv_event_t* e);
    static void OnCountdown(lv_timer_t* timer);
    static void OnTimeout(lv_timer_t* timer);
};

#endif // SCREEN_LAYOUT_H
