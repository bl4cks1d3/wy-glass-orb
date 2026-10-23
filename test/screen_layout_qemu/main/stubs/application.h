#pragma once

#include <string>

// Simulador do Application do firmware: so o que screen_layout.cc e o McpServer usam.
// SendMcpMessage embrulha o payload em {"type":"mcp"} e manda pelo WebSocket (main.cc).
class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    void SendMcpMessage(const std::string& payload);
};
