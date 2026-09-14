#include "AYUI/Clipboard.h"

#if !defined(_WIN32)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <codecvt>
#include <fcntl.h>
#include <locale>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

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

// Audit B-NEW-4: the pre-fix code probed the PATH with
// `std::system("command -v <exe> >/dev/null 2>&1")`. That required the
// shell to parse a command string and so was a tiny shell-injection
// foot-gun (the executable name is currently a hard-coded constant so
// it was harmless, but adding a single dynamic-looking arg would have
// opened the door). Replaced with `execvp` lookup: we try to exec the
// argv[0] with `PATH` resolution via execvp's implicit PATH search,
// and treat EACCES / ENOENT as "missing". This keeps the probe
// shell-free and avoids /bin/sh entirely.
bool executableFound(const char* executable) {
    if (executable == nullptr || executable[0] == '\0') return false;
    // execvp searches PATH for `executable`; if found, it does not
    // return. On failure, errno is set (ENOENT or EACCES).
    pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        // Child: redirect stdout/stderr to /dev/null so the probe is
        // silent, then execvp. execvp handles PATH lookup when the
        // name does not contain '/'.
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        const char* argv[] = {executable, nullptr};
        ::execvp(executable, const_cast<char* const*>(argv));
        ::_exit(127); // execvp only returns on failure
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    // The probe exec is "found" iff execvp succeeded. exec success
    // means the child replaced itself and the wait status is the
    // child's exit (which we don't care about for "found" purposes,
    // since the probe redirected stdout/stderr). A more precise check
    // would be `WIFEXITED(status) && WEXITSTATUS(status) != 127`,
    // but the probe child only exits 127 when execvp fails. Use the
    // exit-status sentinel as the "not found" signal.
    return WIFEXITED(status) && WEXITSTATUS(status) != 127;
}

struct CommandSpec {
    const char* executable;
    std::vector<std::string> writeArgs;
    std::vector<std::string> readArgs;
};

// Convert a vector<string> to the (char* const*) shape execvp wants.
// The storage must outlive execvp; we hand-build a vector<char*>
// that points into the args' c_str() and a trailing nullptr. Returns
// false if the args contained an embedded NUL — execvp would silently
// truncate the argument, which we refuse.
std::vector<char*> toArgv(const std::vector<std::string>& args,
                          bool& ok) {
    std::vector<char*> out;
    out.reserve(args.size() + 1);
    for (const auto& s : args) {
        if (s.find('\0') != std::string::npos) {
            ok = false;
            return {};
        }
        out.push_back(const_cast<char*>(s.c_str()));
    }
    out.push_back(nullptr);
    ok = true;
    return out;
}

std::vector<CommandSpec> clipboardCommands() {
#if defined(__APPLE__)
    return {{{"pbcopy", {"pbcopy"}, {"pbpaste"}}}};
#else
    std::vector<CommandSpec> commands;
    if (std::getenv("WAYLAND_DISPLAY") != nullptr) {
        commands.push_back({"wl-copy",
            {"wl-copy", "--type", "text/plain;charset=utf-8"},
            {"wl-paste", "--no-newline", "--type", "text/plain"}});
    }
    commands.push_back({"xclip",
        {"xclip", "-selection", "clipboard", "-in"},
        {"xclip", "-selection", "clipboard", "-out"}});
    commands.push_back({"xsel",
        {"xsel", "--clipboard", "--input"},
        {"xsel", "--clipboard", "--output"}});
    return commands;
#endif
}

// Spawn `args` (argv[0] is the program) connected to a pipe; the
// pipe's write end goes to the child's stdin (when `toChild` is true)
// or the pipe's read end becomes the child's stdout (when `toChild`
// is false). The parent's matching end is returned in `parentFd`.
// On success returns the child's pid; on failure returns -1 and sets
// `parentFd = -1`.
pid_t spawnWithPipe(const std::vector<std::string>& args,
                    bool toChild, int& parentFd) {
    parentFd = -1;
    if (args.empty()) return -1;
    bool argsOk = false;
    std::vector<char*> argv = toArgv(args, argsOk);
    if (!argsOk) return -1;

    int pipeFds[2] = {-1, -1};
    if (::pipe(pipeFds) != 0) return -1;

    // posix_spawn file actions: close the child's matching end of
    // the pipe in the parent after dup2, and dup2 the kept end onto
    // stdin/stdout.
    posix_spawn_file_actions_t actions;
    if (::posix_spawn_file_actions_init(&actions) != 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return -1;
    }
    if (toChild) {
        // Child reads from pipeFds[0]; we close the write end after dup2.
        ::posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
        ::posix_spawn_file_actions_adddup2(&actions, pipeFds[0], STDIN_FILENO);
        ::posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
        // Also silence the child's stderr; clipboard writes don't need it.
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
            ::posix_spawn_file_actions_addclose(&actions, devnull);
        }
    } else {
        // Child writes to pipeFds[1]; we close the read end after dup2.
        ::posix_spawn_file_actions_addclose(&actions, pipeFds[0]);
        ::posix_spawn_file_actions_adddup2(&actions, pipeFds[1], STDOUT_FILENO);
        ::posix_spawn_file_actions_addclose(&actions, pipeFds[1]);
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
            ::posix_spawn_file_actions_addclose(&actions, devnull);
        }
    }

    pid_t pid = -1;
    const int spawnRc = ::posix_spawn(&pid, args[0].c_str(), &actions,
                                       nullptr, argv.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);

    if (toChild) {
        // Parent keeps the write end.
        ::close(pipeFds[0]);
        parentFd = (spawnRc == 0) ? pipeFds[1] : -1;
        if (spawnRc != 0) ::close(pipeFds[1]);
    } else {
        // Parent keeps the read end.
        ::close(pipeFds[1]);
        parentFd = (spawnRc == 0) ? pipeFds[0] : -1;
        if (spawnRc != 0) ::close(pipeFds[0]);
    }
    return (spawnRc == 0) ? pid : -1;
}

class PosixClipboard final : public IClipboard {
public:
    bool setText(const std::wstring& text) override {
        const std::string utf8 = toUtf8(text);
        if (!text.empty() && utf8.empty()) return false;
        for (const CommandSpec& cmd : clipboardCommands()) {
            if (!executableFound(cmd.executable)) continue;
            int parentFd = -1;
            pid_t pid = spawnWithPipe(cmd.writeArgs, /*toChild=*/true, parentFd);
            if (pid < 0 || parentFd < 0) continue;
            size_t written = 0;
            if (!utf8.empty()) {
                while (written < utf8.size()) {
                    const ssize_t n = ::write(parentFd,
                        utf8.data() + written, utf8.size() - written);
                    if (n <= 0) {
                        if (n < 0 && errno == EINTR) continue;
                        break;
                    }
                    written += static_cast<size_t>(n);
                }
            }
            ::close(parentFd);
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR) continue;
                status = -1;
                break;
            }
            if (written == utf8.size() && status == 0) return true;
        }
        return false;
    }

    bool getText(std::wstring& out) override {
        out.clear();
        constexpr size_t kMaxClipboardBytes = 16u * 1024u * 1024u;
        for (const CommandSpec& cmd : clipboardCommands()) {
            if (!executableFound(cmd.executable)) continue;
            int parentFd = -1;
            pid_t pid = spawnWithPipe(cmd.readArgs, /*toChild=*/false, parentFd);
            if (pid < 0 || parentFd < 0) continue;
            std::string bytes;
            char buffer[4096];
            while (bytes.size() < kMaxClipboardBytes) {
                const ssize_t n = ::read(parentFd, buffer, sizeof(buffer));
                if (n == 0) break;
                if (n < 0) {
                    if (errno == EINTR) continue;
                    break;
                }
                bytes.append(buffer, static_cast<size_t>(n));
            }
            const bool overflow = bytes.size() >= kMaxClipboardBytes;
            ::close(parentFd);
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0) {
                if (errno != EINTR) continue;
                status = -1;
                break;
            }
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
