#include "AYUI/Clipboard.h"

#if !defined(_WIN32)

#include <cstdio>
#include <cstdlib>
#include <codecvt>
#include <locale>
#include <stdexcept>
#include <string>
#include <vector>

namespace ayt::ui {
namespace {

std::string toUtf8(const std::wstring& text) {
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        return converter.to_bytes(text);
    } catch (const std::range_error&) {
        return {};
    }
}

bool fromUtf8(const std::string& text, std::wstring& out) {
    try {
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        out = converter.from_bytes(text);
        return true;
    } catch (const std::range_error&) {
        out.clear();
        return false;
    }
}

bool commandExists(const char* executable) {
    const std::string command = std::string("command -v ") + executable
        + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}

struct CommandPair {
    const char* executable;
    const char* write;
    const char* read;
};

std::vector<CommandPair> clipboardCommands() {
#if defined(__APPLE__)
    return {{"pbcopy", "pbcopy", "pbpaste"}};
#else
    std::vector<CommandPair> commands;
    if (std::getenv("WAYLAND_DISPLAY") != nullptr) {
        commands.push_back({"wl-copy", "wl-copy --type 'text/plain;charset=utf-8'",
                            "wl-paste --no-newline --type text/plain"});
    }
    commands.push_back({"xclip", "xclip -selection clipboard -in",
                        "xclip -selection clipboard -out"});
    commands.push_back({"xsel", "xsel --clipboard --input",
                        "xsel --clipboard --output"});
    return commands;
#endif
}

class PosixClipboard final : public IClipboard {
public:
    bool setText(const std::wstring& text) override {
        const std::string utf8 = toUtf8(text);
        if (!text.empty() && utf8.empty()) return false;
        for (const CommandPair& command : clipboardCommands()) {
            if (!commandExists(command.executable)) continue;
            FILE* pipe = ::popen(command.write, "w");
            if (pipe == nullptr) continue;
            const size_t written = utf8.empty()
                ? 0 : std::fwrite(utf8.data(), 1, utf8.size(), pipe);
            const int status = ::pclose(pipe);
            if (written == utf8.size() && status == 0) return true;
        }
        return false;
    }

    bool getText(std::wstring& out) override {
        out.clear();
        constexpr size_t kMaxClipboardBytes = 16u * 1024u * 1024u;
        for (const CommandPair& command : clipboardCommands()) {
            if (!commandExists(command.executable)) continue;
            FILE* pipe = ::popen(command.read, "r");
            if (pipe == nullptr) continue;
            std::string bytes;
            char buffer[4096];
            while (bytes.size() < kMaxClipboardBytes) {
                const size_t count = std::fread(buffer, 1, sizeof(buffer), pipe);
                bytes.append(buffer, count);
                if (count < sizeof(buffer)) break;
            }
            const bool overflow = bytes.size() >= kMaxClipboardBytes;
            const int status = ::pclose(pipe);
            if (status == 0 && !overflow && !bytes.empty()
                && fromUtf8(bytes, out)) return true;
            out.clear();
        }
        return false;
    }
};

} // namespace

IClipboard* createPosixClipboard() { return new PosixClipboard(); }

} // namespace ayt::ui

#endif
