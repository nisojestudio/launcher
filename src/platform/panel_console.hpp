#pragma once

#include <istream>
#include <ostream>
#include <string>

namespace nlp3::platform {

class PanelApp;

class PanelConsole {
public:
    PanelConsole(PanelApp* app, std::istream* input, std::ostream* output) noexcept;

    void print_overview() const;
    bool execute_line(const std::string& line);
    void print_help() const;

private:
    PanelApp* app_ = nullptr;
    std::istream* input_ = nullptr;
    std::ostream* output_ = nullptr;
};

} // namespace nlp3::platform
