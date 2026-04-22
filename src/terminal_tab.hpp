#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>
#include <vte/vte.h>

#include <string>

#include "shell_detector.hpp"

namespace kiln {

// A single terminal tab: owns a VteTerminal wrapped in a scroller, plus the
// title/icon state the parent window's AdwTabView displays.
class TerminalTab {
public:
    TerminalTab(const ShellInfo& shell, const std::string& cwd);
    ~TerminalTab();

    TerminalTab(const TerminalTab&) = delete;
    TerminalTab& operator=(const TerminalTab&) = delete;

    // The widget to hand to AdwTabView as page content.
    GtkWidget* widget() const { return root_; }
    GtkWidget* vte_widget() const { return terminal_; }
    VteTerminal* vte() const { return VTE_TERMINAL(terminal_); }
    const ShellInfo& shell() const { return shell_; }

    // Associated AdwTabPage (set by the window after appending).
    void set_page(AdwTabPage* page) { page_ = page; }
    AdwTabPage* page() const { return page_; }

    void spawn();
    void copy_clipboard();
    void paste_clipboard();
    void focus_terminal();
    void zoom_in();
    void zoom_out();
    void zoom_reset();
    void apply_color_scheme(bool dark);

    bool has_foreground_process() const;

    // Callback hooks set by the window.
    using CloseHandler = void(*)(TerminalTab*, void*);
    void set_close_handler(CloseHandler h, void* user) {
        on_close_ = h; on_close_user_ = user;
    }

private:
    static void on_child_exited(VteTerminal* vte, int status, gpointer user);
    static void on_title_changed(VteTerminal* vte, gpointer user);
    static void on_bell(VteTerminal* vte, gpointer user);
    static void on_rclick(GtkGestureClick* gesture, int n_press,
                          double x, double y, gpointer user);

    void update_title_from_vte();
    void build_context_menu();

    ShellInfo shell_;
    std::string initial_cwd_;

    GtkWidget* root_ = nullptr;       // GtkScrolledWindow (the page child)
    GtkWidget* terminal_ = nullptr;   // VteTerminal
    GtkWidget* popover_ = nullptr;    // right-click menu
    AdwTabPage* page_ = nullptr;
    double font_scale_ = 1.0;
    guint pending_close_source_ = 0;  // g_idle_add id, 0 when none

    CloseHandler on_close_ = nullptr;
    void* on_close_user_ = nullptr;
};

} // namespace kiln
