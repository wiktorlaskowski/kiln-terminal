#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include <memory>
#include <vector>

#include "shell_detector.hpp"
#include "terminal_tab.hpp"

namespace kiln {

class Window {
public:
    explicit Window(AdwApplication* app);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    GtkWidget* widget() const { return GTK_WIDGET(window_); }

    void present();

    // Open a new tab running the given shell, using the cwd of the currently
    // focused tab if possible.
    void new_tab(const ShellInfo& shell);

    // Open a new tab with the user's default shell.
    void new_default_tab();

    void close_current_tab();

    // Split the focused terminal into two panes along `orient`.
    void split_current(GtkOrientation orient);

    // Apply dark/light scheme change to all tabs.
    void update_color_scheme();

    // Rebuild the "new tab" popover (e.g. if shells list changed).
    void refresh_shell_menu();

private:
    static gboolean on_close_page(AdwTabView* view, AdwTabPage* page, gpointer user);
    static void on_page_attached(AdwTabView* view, AdwTabPage* page,
                                 int position, gpointer user);
    static void on_selected_page_changed(GObject* obj, GParamSpec* pspec,
                                         gpointer user);
    static void on_dark_changed(GObject* obj, GParamSpec* pspec, gpointer user);

    static void tab_close_handler(TerminalTab* tab, void* user);

    void build_ui();
    void wire_actions();
    GMenu* build_shell_menu();
    std::string cwd_of(const TerminalTab* t) const;
    TerminalTab* focused_tab() const;
    TerminalTab* tab_on_page(AdwTabPage* page) const;
    void erase_tab(TerminalTab* t);

    AdwApplication* app_ = nullptr;
    AdwApplicationWindow* window_ = nullptr;
    AdwTabView* tab_view_ = nullptr;
    GtkWidget* tab_bar_ = nullptr;
    GtkWidget* new_tab_button_ = nullptr;

    std::vector<std::unique_ptr<TerminalTab>> tabs_;
};

} // namespace kiln
