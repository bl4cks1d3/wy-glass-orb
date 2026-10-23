#include "screen_layout.h"
#include "display.h"
#include "application.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <cstring>

#define TAG "ScreenLayout"

namespace {

constexpr int kVersion = 1;
constexpr int kColumnWidth = 170;   // area util do circulo de 240 px
constexpr int kMaxButtons = 3;
constexpr int kMaxItems = 6;
constexpr size_t kMaxId = 40;
constexpr size_t kMaxTitle = 60;
constexpr size_t kMaxBody = 120;
constexpr size_t kMaxLabel = 40;

struct ButtonStyle {
    uint32_t bg;
    uint32_t text;
};

ButtonStyle StyleFor(const char* style) {
    if (style && strcmp(style, "primary") == 0) return {0x085041, 0x9FE1CB};
    if (style && strcmp(style, "danger") == 0) return {0x501313, 0xF7C1C1};
    return {0x26215C, 0xCECBF6};
}

std::string Clip(const cJSON* item, size_t max_len) {
    if (!cJSON_IsString(item)) return "";
    std::string s = item->valuestring;
    if (s.size() <= max_len) return s;
    // Corta sem partir um caractere UTF-8 no meio.
    size_t cut = max_len;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
    return s.substr(0, cut);
}

std::string JsonResult(bool ok, const std::string& error = "") {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", ok);
    if (!ok) cJSON_AddStringToObject(root, "error", error.c_str());
    char* str = cJSON_PrintUnformatted(root);
    std::string result(str);
    cJSON_free(str);
    cJSON_Delete(root);
    return result;
}

lv_obj_t* AddLabel(lv_obj_t* parent, const std::string& text, uint32_t color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, kColumnWidth);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text.c_str());
    return label;
}

}  // namespace

std::string ScreenLayout::Show(Display* display, const std::string& json) {
    cJSON* layout = cJSON_Parse(json.c_str());
    if (!cJSON_IsObject(layout)) {
        cJSON_Delete(layout);
        return "layout is not a JSON object";
    }
    std::string error;
    const cJSON* v = cJSON_GetObjectItem(layout, "v");
    const cJSON* id = cJSON_GetObjectItem(layout, "id");
    if (!cJSON_IsNumber(v) || v->valueint != kVersion) {
        error = "unsupported layout version (expected v=1)";
    } else if (!cJSON_IsString(id) || id->valuestring[0] == '\0') {
        error = "layout.id is required";
    } else if (!cJSON_GetObjectItem(layout, "title") && !cJSON_GetObjectItem(layout, "body") &&
               !cJSON_GetObjectItem(layout, "items") && !cJSON_GetObjectItem(layout, "progress") &&
               !cJSON_GetObjectItem(layout, "timer") && !cJSON_GetObjectItem(layout, "buttons")) {
        error = "layout has nothing to show";
    }
    if (!error.empty()) {
        cJSON_Delete(layout);
        return error;
    }

    {
        DisplayLockGuard lock(display);
        Teardown(false);
        Build(layout);
    }
    cJSON_Delete(layout);
    ESP_LOGI(TAG, "Showing layout %s", layout_id_.c_str());
    return "";
}

void ScreenLayout::Close(Display* display) {
    DisplayLockGuard lock(display);
    Teardown(false);
}

void ScreenLayout::Build(cJSON* layout) {
    generation_++;
    layout_id_ = Clip(cJSON_GetObjectItem(layout, "id"), kMaxId);
    elements_.clear();

    const cJSON* close_on_tap_json = cJSON_GetObjectItem(layout, "close_on_tap");
    bool close_on_tap = !cJSON_IsBool(close_on_tap_json) || cJSON_IsTrue(close_on_tap_json);

    root_ = lv_obj_create(lv_layer_top());
    lv_obj_set_size(root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(root_);
    lv_obj_set_style_radius(root_, 0, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(0x0B0B0B), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root_, lv_obj_get_style_text_font(lv_screen_active(), LV_PART_MAIN), 0);
    lv_obj_set_style_pad_all(root_, 35, 0);
    lv_obj_set_style_pad_row(root_, 8, 0);
    lv_obj_set_flex_flow(root_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(root_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    // Bloqueia os toques para a interface que fica por baixo.
    lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);

    std::string title = Clip(cJSON_GetObjectItem(layout, "title"), kMaxTitle);
    if (!title.empty()) {
        AddLabel(root_, title, 0xF1EFE8);
    }
    std::string body = Clip(cJSON_GetObjectItem(layout, "body"), kMaxBody);
    if (!body.empty()) {
        AddLabel(root_, body, 0xB4B2A9);
    }

    const cJSON* items = cJSON_GetObjectItem(layout, "items");
    if (cJSON_IsArray(items)) {
        int count = 0;
        const cJSON* item;
        cJSON_ArrayForEach(item, items) {
            if (count++ >= kMaxItems) break;
            std::string item_id = Clip(cJSON_GetObjectItem(item, "id"), kMaxId);
            if (item_id.empty()) continue;
            lv_obj_t* box = lv_checkbox_create(root_);
            lv_checkbox_set_text(box, Clip(cJSON_GetObjectItem(item, "text"), kMaxLabel).c_str());
            lv_obj_set_width(box, kColumnWidth);
            lv_obj_set_style_text_color(box, lv_color_hex(0xF1EFE8), 0);
            if (cJSON_IsTrue(cJSON_GetObjectItem(item, "checked"))) {
                lv_obj_add_state(box, LV_STATE_CHECKED);
            }
            elements_.push_back({item_id, false});
            lv_obj_add_event_cb(box, OnItem, LV_EVENT_VALUE_CHANGED, Tag(elements_.size() - 1));
        }
    }

    const cJSON* progress = cJSON_GetObjectItem(layout, "progress");
    if (cJSON_IsNumber(progress)) {
        lv_obj_t* bar = lv_bar_create(root_);
        lv_obj_set_size(bar, 150, 8);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, LV_CLAMP(0, progress->valueint, 100), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x444441), LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, lv_color_hex(0x7F77DD), LV_PART_INDICATOR);
    }

    const cJSON* timer = cJSON_GetObjectItem(layout, "timer");
    const cJSON* seconds = cJSON_IsObject(timer) ? cJSON_GetObjectItem(timer, "seconds") : nullptr;
    if (cJSON_IsNumber(seconds)) {
        remaining_s_ = LV_CLAMP(1, seconds->valueint, 5999);
        timer_label_ = lv_label_create(root_);
#if LV_FONT_MONTSERRAT_48
        lv_obj_set_style_text_font(timer_label_, &lv_font_montserrat_48, 0);
#endif
        lv_obj_set_style_text_color(timer_label_, lv_color_hex(0xF1EFE8), 0);
        UpdateTimerLabel();
        countdown_ = lv_timer_create(OnCountdown, 1000, this);
    }

    const cJSON* buttons = cJSON_GetObjectItem(layout, "buttons");
    if (cJSON_IsArray(buttons) && cJSON_GetArraySize(buttons) > 0) {
        lv_obj_t* row = lv_obj_create(root_);
        lv_obj_set_size(row, kColumnWidth, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        int count = 0;
        const cJSON* button;
        cJSON_ArrayForEach(button, buttons) {
            if (count++ >= kMaxButtons) break;
            std::string button_id = Clip(cJSON_GetObjectItem(button, "id"), kMaxId);
            if (button_id.empty()) continue;
            const cJSON* style_json = cJSON_GetObjectItem(button, "style");
            ButtonStyle style = StyleFor(cJSON_IsString(style_json) ? style_json->valuestring : nullptr);

            lv_obj_t* btn = lv_button_create(row);
            lv_obj_set_style_bg_color(btn, lv_color_hex(style.bg), 0);
            lv_obj_set_style_radius(btn, 16, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_hor(btn, 12, 0);
            lv_obj_set_style_pad_ver(btn, 6, 0);
            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text(label, Clip(cJSON_GetObjectItem(button, "text"), kMaxLabel).c_str());
            lv_obj_set_style_text_color(label, lv_color_hex(style.text), 0);

            elements_.push_back({button_id, close_on_tap});
            lv_obj_add_event_cb(btn, OnButton, LV_EVENT_CLICKED, Tag(elements_.size() - 1));
        }
    }

    const cJSON* timeout_s = cJSON_GetObjectItem(layout, "timeout_s");
    if (cJSON_IsNumber(timeout_s) && timeout_s->valueint > 0) {
        timeout_ = lv_timer_create(OnTimeout, LV_CLAMP(1, timeout_s->valueint, 600) * 1000, this);
        lv_timer_set_repeat_count(timeout_, 1);
        lv_timer_set_auto_delete(timeout_, false);
    }

    open_ = true;
}

void ScreenLayout::Teardown(bool async) {
    if (countdown_) {
        lv_timer_delete(countdown_);
        countdown_ = nullptr;
    }
    if (timeout_) {
        lv_timer_delete(timeout_);
        timeout_ = nullptr;
    }
    if (root_) {
        // Dentro de um callback de evento do proprio objeto, so da para apagar de forma assincrona.
        if (async) {
            lv_obj_delete_async(root_);
        } else {
            lv_obj_delete(root_);
        }
        root_ = nullptr;
    }
    timer_label_ = nullptr;
    // Eventos ainda pendentes do layout antigo sao descartados pela geracao.
    generation_++;
    open_ = false;
}

void* ScreenLayout::Tag(size_t index) const {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(((generation_ & 0xFFFFFF) << 8) | (index & 0xFF)));
}

bool ScreenLayout::Untag(void* user_data, size_t* index) const {
    auto tag = reinterpret_cast<uintptr_t>(user_data);
    if ((tag >> 8) != (generation_ & 0xFFFFFF)) return false;
    *index = tag & 0xFF;
    return *index < elements_.size();
}

void ScreenLayout::SendEvent(const std::string& element, const char* event, int checked) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "notifications/ui_event");
    cJSON* params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "layout", layout_id_.c_str());
    cJSON_AddStringToObject(params, "element", element.c_str());
    cJSON_AddStringToObject(params, "event", event);
    if (checked >= 0) cJSON_AddBoolToObject(params, "checked", checked == 1);
    cJSON_AddItemToObject(root, "params", params);
    char* str = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "ui_event: %s", str);
    Application::GetInstance().SendMcpMessage(str);
    cJSON_free(str);
    cJSON_Delete(root);
}

void ScreenLayout::UpdateTimerLabel() {
    if (!timer_label_) return;
    lv_label_set_text_fmt(timer_label_, "%02d:%02d", remaining_s_ / 60, remaining_s_ % 60);
}

void ScreenLayout::OnButton(lv_event_t* e) {
    auto& self = GetInstance();
    size_t index;
    if (!self.Untag(lv_event_get_user_data(e), &index)) return;
    Element element = self.elements_[index];
    self.SendEvent(element.id, "tap");
    if (element.close_on_tap) {
        self.Teardown(true);
    }
}

void ScreenLayout::OnItem(lv_event_t* e) {
    auto& self = GetInstance();
    size_t index;
    if (!self.Untag(lv_event_get_user_data(e), &index)) return;
    bool checked = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(e)), LV_STATE_CHECKED);
    self.SendEvent(self.elements_[index].id, "toggle", checked ? 1 : 0);
}

void ScreenLayout::OnCountdown(lv_timer_t* timer) {
    auto* self = static_cast<ScreenLayout*>(lv_timer_get_user_data(timer));
    if (self->remaining_s_ <= 0) return;
    self->remaining_s_--;
    self->UpdateTimerLabel();
    if (self->remaining_s_ == 0) {
        self->SendEvent("timer", "timer_done");
        lv_timer_pause(timer);
    }
}

void ScreenLayout::OnTimeout(lv_timer_t* timer) {
    auto* self = static_cast<ScreenLayout*>(lv_timer_get_user_data(timer));
    self->SendEvent("", "timeout");
    self->Teardown(true);
}

void ScreenLayout::RegisterMcpTools(Display* display) {
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddTool("self.screen.render",
        "Show an interactive screen on the device's round 240x240 display, on top of the chat UI.\n"
        "Use it for confirmations, short lists, timers and progress. Keep it short: there is no scrolling.\n"
        "Args:\n"
        "  `layout`: a JSON string. Fields: v (must be 1), id (required, echoed back), title (<=60 chars), "
        "body (<=120 chars), items (<=6 of {id,text,checked}), progress (0-100), timer ({seconds}), "
        "buttons (<=3 of {id,text,style: primary|danger|default}), close_on_tap (default true), timeout_s.\n"
        "Return:\n"
        "  {\"success\":true} or {\"success\":false,\"error\":\"...\"}.\n"
        "Taps arrive later as notifications/ui_event with {layout, element, event: tap|toggle|timer_done|timeout}.",
        PropertyList({
            Property("layout", kPropertyTypeString)
        }),
        [display](const PropertyList& properties) -> ReturnValue {
            std::string error = GetInstance().Show(display, properties["layout"].value<std::string>());
            return error.empty() ? JsonResult(true) : JsonResult(false, error);
        });

    mcp_server.AddTool("self.screen.close",
        "Close the interactive screen opened by self.screen.render and go back to the chat UI.",
        PropertyList(),
        [display](const PropertyList& properties) -> ReturnValue {
            GetInstance().Close(display);
            return JsonResult(true);
        });
}
