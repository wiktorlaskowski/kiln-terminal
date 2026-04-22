#include "shell_detector.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <pwd.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace fs = std::filesystem;

namespace kiln {

namespace {

struct ShellMeta {
    const char* display;
    const char* icon;
};

// Known shells get a pretty display name and a best-guess icon. Icon names are
// resolved by the active GTK icon theme; if missing, callers should fall back
// to "utilities-terminal-symbolic".
const std::unordered_map<std::string, ShellMeta>& known_shells() {
    static const std::unordered_map<std::string, ShellMeta> map = {
        {"bash",      {"Bash",             "utilities-terminal"}},
        {"sh",        {"Bourne Shell",     "utilities-terminal"}},
        {"dash",      {"Dash",             "utilities-terminal"}},
        {"zsh",       {"Zsh",              "utilities-terminal"}},
        {"fish",      {"Fish",             "utilities-terminal"}},
        {"ksh",       {"KornShell",        "utilities-terminal"}},
        {"mksh",      {"MirBSD KornShell", "utilities-terminal"}},
        {"tcsh",      {"Tcsh",             "utilities-terminal"}},
        {"csh",       {"C Shell",          "utilities-terminal"}},
        {"elvish",    {"Elvish",           "utilities-terminal"}},
        {"nu",        {"Nushell",          "utilities-terminal"}},
        {"nushell",   {"Nushell",          "utilities-terminal"}},
        {"xonsh",     {"Xonsh",            "utilities-terminal"}},
        {"pwsh",      {"PowerShell",       "utilities-terminal"}},
        {"ion",       {"Ion",              "utilities-terminal"}},
        {"oil",       {"Oil",              "utilities-terminal"}},
        {"osh",       {"Oil (osh)",        "utilities-terminal"}},
        {"yash",      {"Yash",             "utilities-terminal"}},
    };
    return map;
}

bool is_executable_file(const std::string& path) {
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0) return false;
    if (!S_ISREG(st.st_mode)) return false;
    return ::access(path.c_str(), X_OK) == 0;
}

std::string basename_of(const std::string& path) {
    auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string title_case(std::string s) {
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(
        static_cast<unsigned char>(s[0])));
    return s;
}

ShellInfo make_info(const std::string& path) {
    ShellInfo info;
    info.path = path;
    info.name = basename_of(path);

    const auto& known = known_shells();
    auto it = known.find(info.name);
    if (it != known.end()) {
        info.display = it->second.display;
        info.icon    = it->second.icon;
    } else {
        info.display = title_case(info.name);
        info.icon    = "utilities-terminal";
    }
    return info;
}

std::vector<std::string> read_etc_shells() {
    std::vector<std::string> out;
    std::ifstream in("/etc/shells");
    std::string line;
    // Cap to avoid a pathological /etc/shells (or a symlink to /dev/zero) from
    // hanging startup. 1024 entries is wildly more than any sane system.
    constexpr size_t kMaxLines = 1024;
    constexpr size_t kMaxLineLen = 4096;
    while (out.size() < kMaxLines && std::getline(in, line)) {
        if (line.size() > kMaxLineLen) continue;
        auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        if (line[first] == '#') continue;
        auto last = line.find_last_not_of(" \t\r\n");
        auto entry = line.substr(first, last - first + 1);
        // Only accept absolute paths with no embedded control chars.
        if (entry.empty() || entry.front() != '/') continue;
        if (entry.find_first_of("\n\r\0", 0, 3) != std::string::npos) continue;
        out.emplace_back(std::move(entry));
    }
    return out;
}

// Extra candidate paths for shells that may exist but not be listed in
// /etc/shells (e.g. on systems where the user installed fish into /usr/local).
std::vector<std::string> candidate_paths() {
    return {
        "/bin/sh", "/bin/bash", "/bin/dash", "/bin/zsh", "/bin/fish",
        "/bin/ksh", "/bin/mksh", "/bin/tcsh", "/bin/csh", "/bin/yash",
        "/usr/bin/bash", "/usr/bin/dash", "/usr/bin/zsh", "/usr/bin/fish",
        "/usr/bin/ksh", "/usr/bin/mksh", "/usr/bin/tcsh", "/usr/bin/csh",
        "/usr/bin/elvish", "/usr/bin/nu", "/usr/bin/xonsh", "/usr/bin/pwsh",
        "/usr/bin/ion", "/usr/bin/osh", "/usr/bin/oil", "/usr/bin/yash",
        "/usr/local/bin/bash", "/usr/local/bin/zsh", "/usr/local/bin/fish",
        "/usr/local/bin/nu", "/usr/local/bin/elvish", "/usr/local/bin/xonsh",
        "/snap/bin/pwsh",
    };
}

std::string canonical(const std::string& path) {
    std::error_code ec;
    auto p = fs::canonical(path, ec);
    return ec ? path : p.string();
}

} // namespace

ShellInfo default_shell() {
    std::string path;
    if (const char* env = std::getenv("SHELL"); env && *env) {
        path = env;
    } else if (auto* pw = ::getpwuid(::getuid()); pw && pw->pw_shell && *pw->pw_shell) {
        path = pw->pw_shell;
    }
    if (path.empty() || !is_executable_file(path)) {
        path = "/bin/sh";
    }
    return make_info(path);
}

std::vector<ShellInfo> detect_shells() {
    std::set<std::string> seen_canon;
    std::vector<ShellInfo> results;

    auto try_add = [&](const std::string& raw) {
        if (raw.empty()) return;
        if (!is_executable_file(raw)) return;
        auto canon = canonical(raw);
        if (!seen_canon.insert(canon).second) return;
        results.push_back(make_info(raw));
    };

    // Default first, so it naturally ends up near the top.
    auto def = default_shell();
    try_add(def.path);

    for (const auto& p : read_etc_shells()) try_add(p);
    for (const auto& p : candidate_paths()) try_add(p);

    // Stable: default first, rest alphabetical by display name.
    if (results.size() > 1) {
        std::sort(results.begin() + 1, results.end(),
            [](const ShellInfo& a, const ShellInfo& b) {
                return a.display < b.display;
            });
    }
    return results;
}

} // namespace kiln
