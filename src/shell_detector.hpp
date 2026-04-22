#pragma once

#include <string>
#include <vector>

namespace kiln {

struct ShellInfo {
    std::string name;      // e.g. "bash"
    std::string path;      // e.g. "/bin/bash"
    std::string display;   // e.g. "Bash"
    std::string icon;      // icon-theme name
    std::string version;   // best-effort, may be empty
};

// Returns all detected, executable shells on the system, de-duplicated by
// canonical path, sorted so the user's default shell is first.
std::vector<ShellInfo> detect_shells();

// Returns the user's login shell (from $SHELL or /etc/passwd), falling back
// to /bin/sh. The returned ShellInfo is always usable.
ShellInfo default_shell();

} // namespace kiln
