// Partes do firmware que o teste nao compila (display.cc e mcp_server.cc puxam a placa
// inteira). Display vira casca vazia; o McpServer repete o tratamento de initialize,
// tools/list e tools/call do main/mcp_server.cc usando o mesmo header.
#include "display.h"
#include "mcp_server.h"
#include "application.h"

#include <esp_log.h>
#include <algorithm>
#include <cstring>

#define TAG "stubs"

Display::Display() {}
Display::~Display() {}
void Display::SetStatus(const char* status) {}
void Display::ShowNotification(const char* notification, int duration_ms) {}
void Display::ShowNotification(const std::string& notification, int duration_ms) {}
void Display::SetEmotion(const char* emotion) {}
void Display::SetChatMessage(const char* role, const char* content) {}
void Display::SetIcon(const char* icon) {}
void Display::SetPreviewImage(const lv_img_dsc_t* image) {}
void Display::SetTheme(const std::string& theme_name) {}
void Display::UpdateStatusBar(bool update_all) {}

McpServer::McpServer() {}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
}

void McpServer::AddTool(McpTool* tool) {
    ESP_LOGI(TAG, "Add tool: %s", tool->name().c_str());
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties,
                        std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json) {
        ParseMessage(json);
        cJSON_Delete(json);
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    auto method = cJSON_GetObjectItem(json, "method");
    auto id = cJSON_GetObjectItem(json, "id");
    if (!cJSON_IsString(method) || strncmp(method->valuestring, "notifications", 13) == 0 || !cJSON_IsNumber(id)) {
        return;
    }
    auto params = cJSON_GetObjectItem(json, "params");
    std::string method_str = method->valuestring;
    if (method_str == "initialize") {
        ReplyResult(id->valueint, "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},"
                                  "\"serverInfo\":{\"name\":\"screen-layout-qemu\",\"version\":\"test\"}}");
    } else if (method_str == "tools/list") {
        GetToolsList(id->valueint, "");
    } else if (method_str == "tools/call") {
        auto name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(name)) {
            ReplyError(id->valueint, "Missing name");
            return;
        }
        DoToolCall(id->valueint, name->valuestring, cJSON_GetObjectItem(params, "arguments"));
    } else {
        ReplyError(id->valueint, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    Application::GetInstance().SendMcpMessage("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                                              ",\"result\":" + result + "}");
}

void McpServer::ReplyError(int id, const std::string& message) {
    cJSON* error = cJSON_CreateObject();
    cJSON_AddStringToObject(error, "message", message.c_str());
    char* str = cJSON_PrintUnformatted(error);
    Application::GetInstance().SendMcpMessage("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                                              ",\"error\":" + str + "}");
    cJSON_free(str);
    cJSON_Delete(error);
}

void McpServer::GetToolsList(int id, const std::string& cursor) {
    std::string tools = "[";
    for (size_t i = 0; i < tools_.size(); i++) {
        if (i) tools += ",";
        tools += tools_[i]->to_json();
    }
    ReplyResult(id, "{\"tools\":" + tools + "]}");
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments) {
    auto it = std::find_if(tools_.begin(), tools_.end(), [&](McpTool* t) { return t->name() == tool_name; });
    if (it == tools_.end()) {
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }
    PropertyList properties = (*it)->properties();
    for (auto& property : properties) {
        auto value = cJSON_GetObjectItem(tool_arguments, property.name().c_str());
        if (property.type() == kPropertyTypeString && cJSON_IsString(value)) {
            property.set_value<std::string>(value->valuestring);
        } else if (property.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
            property.set_value<int>(value->valueint);
        } else if (property.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
            property.set_value<bool>(cJSON_IsTrue(value));
        } else if (!property.has_default_value()) {
            ReplyError(id, "Missing valid argument: " + property.name());
            return;
        }
    }
    ReplyResult(id, (*it)->Call(properties));
}
