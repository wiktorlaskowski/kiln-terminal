#include "terminal_tab.hpp"

#include <algorithm>
#include <cstring>
#include <signal.h>
#include <string>
#include <vector>

namespace kiln {

namespace {

// Standard VGA-ish 16-color palette, tuned for readability on Adwaita.
const GdkRGBA kPalette[16] = {
    {0.156f, 0.164f, 0.211f, 1.0f}, // black
    {0.937f, 0.325f, 0.313f, 1.0f}, // red
    {0.541f, 0.886f, 0.203f, 1.0f}, // green
    {0.988f, 0.913f, 0.309f, 1.0f}, // yellow
    {0.447f, 0.623f, 0.811f, 1.0f}, // blue
    {0.827f, 0.529f, 0.921f, 1.0f}, // magenta
    {0.203f, 0.764f, 0.776f, 1.0f}, // cyan
    {0.933f, 0.933f, 0.925f, 1.0f}, // white
    {0.333f, 0.341f, 0.388f, 1.0f},
    {0.988f, 0.474f, 0.466f, 1.0f},
    {0.639f, 0.988f, 0.376f, 1.0f},
    {1.000f, 0.960f, 0.466f, 1.0f},
    {0.600f, 0.760f, 0.933f, 1.0f},
    {0.913f, 0.670f, 0.960f, 1.0f},
    {0.356f, 0.866f, 0.866f, 1.0f},
    {1.000f, 1.000f, 1.000f, 1.0f},
};

const GdkRGBA kLightBg = {0.988f, 0.988f, 0.988f, 1.0f};
const GdkRGBA kLightFg = {0.156f, 0.164f, 0.211f, 1.0f};
const GdkRGBA kDarkBg  = {0.117f, 0.117f, 0.180f, 1.0f};
const GdkRGBA kDarkFg  = {0.933f, 0.933f, 0.925f, 1.0f};

std::vector<std::string> build_env_additions() {
    return {
        std::string("TERM_PROGRAM=") + "KilnTerminal",
        std::string("COLORTERM=") + "truecolor",
    };
}

} // namespace

TerminalTab::TerminalTab(const ShellInfo& shell, const std::string& cwd)
    : shell_(shell), initial_cwd_(cwd)
{
    terminal_ = vte_terminal_new();

    auto* vte = VTE_TERMINAL(terminal_);
    vte_terminal_set_scrollback_lines(vte, 10000);
    vte_terminal_set_mouse_autohide(vte, TRUE);
    vte_terminal_set_scroll_on_keystroke(vte, TRUE);
    vte_terminal_set_scroll_on_output(vte, FALSE);
    vte_terminal_set_allow_hyperlink(vte, TRUE);
    vte_terminal_set_audible_bell(vte, FALSE);
    vte_terminal_set_enable_sixel(vte, TRUE);

    // Use system monospace font through pango description pulled from the
    // current GtkSettings (so changing font in Tweaks applies live-ish).
    auto* settings = gtk_settings_get_default();
    gchar* font_name = nullptr;
    g_object_get(settings, "gtk-font-name", &font_name, nullptr);
    PangoFontDescription* desc = pango_font_description_from_string("Monospace 11");
    (void)font_name;
    g_free(font_name);
    vte_terminal_set_font(vte, desc);
    pango_font_description_free(desc);

    // Pick initial colors based on the system dark preference.
    auto* style_mgr = adw_style_manager_get_default();
    bool dark = adw_style_manager_get_dark(style_mgr);
    apply_color_scheme(dark);

    root_ = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(root_),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(root_), terminal_);
    gtk_widget_set_hexpand(terminal_, TRUE);
    gtk_widget_set_vexpand(terminal_, TRUE);

    g_signal_connect(vte, "child-exited",
        G_CALLBACK(&TerminalTab::on_child_exited), this);
    g_signal_connect(vte, "window-title-changed",
        G_CALLBACK(&TerminalTab::on_title_changed), this);
    g_signal_connect(vte, "bell",
        G_CALLBACK(&TerminalTab::on_bell), this);

    build_context_menu();
}

TerminalTab::~TerminalTab() {
    // Cancel any pending deferred close so it can't fire against a freed tab.
    if (pending_close_source_ != 0) {
        g_source_remove(pending_close_source_);
        pending_close_source_ = 0;
    }
    // Break any VTE signal callbacks that still reference `this`, since
    // the VteTerminal widget may outlive us (held by GTK ref until AdwTabView
    // finishes closing the page).
    if (terminal_) {
        g_signal_handlers_disconnect_by_data(terminal_, this);
    }
    // The popover is parented to the VteTerminal; unparent it so it doesn't
    // outlive us with a dangling user-data pointer.
    if (popover_) {
        gtk_widget_unparent(popover_);
        popover_ = nullptr;
    }
}

void TerminalTab::build_context_menu() {
    GMenu* menu = g_menu_new();

    GMenu* s_edit = g_menu_new();
    g_menu_append(s_edit, "Copy",       "win.copy");
    g_menu_append(s_edit, "Paste",      "win.paste");
    g_menu_append(s_edit, "Select All", "win.select-all");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(s_edit));
    g_object_unref(s_edit);

    GMenu* s_split = g_menu_new();
    g_menu_append(s_split, "Split Right", "win.split-h");
    g_menu_append(s_split, "Split Down",  "win.split-v");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(s_split));
    g_object_unref(s_split);

    GMenu* s_tabs = g_menu_new();
    g_menu_append(s_tabs, "New Tab",  "win.new-default-tab");
    g_menu_append(s_tabs, "Close",    "win.close-tab");
    g_menu_append_section(menu, nullptr, G_MENU_MODEL(s_tabs));
    g_object_unref(s_tabs);

    popover_ = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    g_object_unref(menu);
    gtk_popover_set_has_arrow(GTK_POPOVER(popover_), FALSE);
    gtk_widget_set_halign(popover_, GTK_ALIGN_START);
    gtk_widget_set_parent(popover_, terminal_);

    auto* gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture),
        GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed",
        G_CALLBACK(&TerminalTab::on_rclick), this);
    gtk_widget_add_controller(terminal_, GTK_EVENT_CONTROLLER(gesture));
}

void TerminalTab::on_rclick(GtkGestureClick* gesture, int /*n_press*/,
                            double x, double y, gpointer user) {
    auto* self = static_cast<TerminalTab*>(user);
    if (!self->popover_) return;
    // Claim the gesture so VTE doesn't treat this as a text paste / selection.
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
    // Take focus so the popover's win.* actions target this terminal.
    gtk_widget_grab_focus(self->terminal_);
    GdkRectangle r = {static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(self->popover_), &r);
    gtk_popover_popup(GTK_POPOVER(self->popover_));
}

void TerminalTab::apply_color_scheme(bool dark) {
    const GdkRGBA& fg = dark ? kDarkFg : kLightFg;
    const GdkRGBA& bg = dark ? kDarkBg : kLightBg;
    vte_terminal_set_colors(VTE_TERMINAL(terminal_), &fg, &bg, kPalette, 16);
}

void TerminalTab::spawn() {
    // With G_SPAWN_FILE_AND_ARGV_ZERO, argv[0] is the file to exec and argv[1]
    // becomes the child's argv[0]. Prefixing with "-" asks the shell to start
    // as a login shell (reads ~/.profile, ~/.bash_profile, etc.).
    std::string dash_name = "-" + shell_.name;
    char* argv[] = {
        const_cast<char*>(shell_.path.c_str()),
        const_cast<char*>(dash_name.c_str()),
        nullptr,
    };

    gchar** current_env = g_get_environ();
    for (const auto& kv : build_env_additions()) {
        auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        current_env = g_environ_setenv(current_env,
            kv.substr(0, eq).c_str(),
            kv.substr(eq + 1).c_str(),
            TRUE);
    }

    const char* cwd = initial_cwd_.empty() ? g_get_home_dir()
                                           : initial_cwd_.c_str();

    vte_terminal_spawn_async(
        VTE_TERMINAL(terminal_),
        VTE_PTY_DEFAULT,
        cwd,
        argv,
        current_env,
        G_SPAWN_FILE_AND_ARGV_ZERO,
        nullptr, nullptr, nullptr,
        -1,
        nullptr,
        +[](VteTerminal*, GPid pid, GError* err, gpointer user) {
            (void)pid;
            if (err) {
                auto* self = static_cast<TerminalTab*>(user);
                gchar* msg = g_strdup_printf(
                    "\r\n\x1b[1;31mFailed to launch %s:\x1b[0m %s\r\n",
                    self->shell_.path.c_str(), err->message);
                vte_terminal_feed(VTE_TERMINAL(self->terminal_), msg, -1);
                g_free(msg);
            }
        },
        this);

    g_strfreev(current_env);
}

void TerminalTab::copy_clipboard() {
    if (vte_terminal_get_has_selection(VTE_TERMINAL(terminal_))) {
        vte_terminal_copy_clipboard_format(VTE_TERMINAL(terminal_),
            VTE_FORMAT_TEXT);
    }
}

void TerminalTab::paste_clipboard() {
    vte_terminal_paste_clipboard(VTE_TERMINAL(terminal_));
}

void TerminalTab::focus_terminal() {
    gtk_widget_grab_focus(terminal_);
}

void TerminalTab::zoom_in() {
    font_scale_ = std::min(font_scale_ + 0.1, 4.0);
    vte_terminal_set_font_scale(VTE_TERMINAL(terminal_), font_scale_);
}

void TerminalTab::zoom_out() {
    font_scale_ = std::max(font_scale_ - 0.1, 0.3);
    vte_terminal_set_font_scale(VTE_TERMINAL(terminal_), font_scale_);
}

void TerminalTab::zoom_reset() {
    font_scale_ = 1.0;
    vte_terminal_set_font_scale(VTE_TERMINAL(terminal_), font_scale_);
}

bool TerminalTab::has_foreground_process() const {
    GPid pid = vte_terminal_get_pty(VTE_TERMINAL(terminal_))
        ? vte_pty_get_fd(vte_terminal_get_pty(VTE_TERMINAL(terminal_)))
        : -1;
    (void)pid;
    // Simple heuristic: if child is still alive, the tab is "busy".
    // VTE emits child-exited when the shell ends, so we conservatively say no.
    return false;
}

void TerminalTab::update_title_from_vte() {
    if (!page_) return;
    const char* title = vte_terminal_get_window_title(VTE_TERMINAL(terminal_));
    if (title && *title) {
        // Sanitize: drop control chars and clamp length so a hostile program
        // can't DoS the tab bar with a megabyte-long title.
        constexpr size_t kMaxLen = 256;
        std::string cleaned;
        cleaned.reserve(std::min<size_t>(strlen(title), kMaxLen));
        for (const char* p = title; *p && cleaned.size() < kMaxLen; ++p) {
            unsigned char c = static_cast<unsigned char>(*p);
            if (c == '\n' || c == '\r' || c == '\t') cleaned.push_back(' ');
            else if (c >= 0x20 || (c & 0x80)) cleaned.push_back(*p);
        }
        if (cleaned.empty()) cleaned = shell_.display;
        adw_tab_page_set_title(page_, cleaned.c_str());
    } else {
        adw_tab_page_set_title(page_, shell_.display.c_str());
    }
}

void TerminalTab::on_title_changed(VteTerminal*, gpointer user) {
    static_cast<TerminalTab*>(user)->update_title_from_vte();
}

void TerminalTab::on_bell(VteTerminal*, gpointer user) {
    auto* self = static_cast<TerminalTab*>(user);
    if (self->page_) adw_tab_page_set_needs_attention(self->page_, TRUE);
}

void TerminalTab::on_child_exited(VteTerminal*, int /*status*/, gpointer user) {
    auto* self = static_cast<TerminalTab*>(user);
    if (self->pending_close_source_ != 0) return;  // already scheduled
    // Defer to the main loop: we must not destroy `self` while still unwinding
    // from a VTE signal emission on `self->terminal_`.
    self->pending_close_source_ = g_idle_add(
        +[](gpointer u) -> gboolean {
            auto* s = static_cast<TerminalTab*>(u);
            s->pending_close_source_ = 0;
            if (s->on_close_) s->on_close_(s, s->on_close_user_);
            return G_SOURCE_REMOVE;
        },
        self);
}

} // namespace kiln
